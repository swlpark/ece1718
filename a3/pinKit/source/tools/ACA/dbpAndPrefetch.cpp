#include "dbpAndPrefetch.h" 

std::map <size_t, TraceEntry> tr_hist_tbl; //accessed by L1-I and L1-D caches
std::map <size_t, RefEntry> ref_hist_tbl;  //accessed by L2 cache
std::map <size_t, std::list<PredEntry> > l1_tcp_pred_tbl; 
std::map <size_t, std::list<PredEntry> > l2_tcp_pred_tbl;

bool TcpEnabled = false;
bool UseCacheBurst = false;

void update_trace(size_t blk_addr, size_t pc)
{
    if(tr_hist_tbl.find(blk_addr) == tr_hist_tbl.end()) 
    {
      std::cout << "BLK_ADDR:" << blk_addr << std::endl;
      std::cout << "PC:" << pc << std::endl;
    }
    assert(tr_hist_tbl.find(blk_addr) != tr_hist_tbl.end());
    tr_hist_tbl[blk_addr].current_trace += pc;
    tr_hist_tbl[blk_addr].current_trace &= ((1 << 30) - 1);
}

bool predict_db_trace(size_t blk_addr)
{
  if (tr_hist_tbl.find(blk_addr) != tr_hist_tbl.end())
  {
    if((tr_hist_tbl[blk_addr].current_trace == tr_hist_tbl[blk_addr].old_trace) && tr_hist_tbl[blk_addr].confidence)
    {
      return true;
    }
  }
  return false;
}

bool predict_db_cnt(size_t blk_addr, int ref_cnt)
{
  if (ref_hist_tbl.find(blk_addr) != ref_hist_tbl.end())
  {
    if((ref_hist_tbl[blk_addr].dead_cnt == ref_cnt) && ref_hist_tbl[blk_addr].sat_cnt == 1)
    {
      return true;
    }
  }
  return false;
}

void update_on_miss_cnt(size_t blk_addr)
{
    if (ref_hist_tbl.find(blk_addr) == ref_hist_tbl.end() )
    {
       RefEntry new_tr;
       new_tr.sat_cnt = 0;
       new_tr.dead_cnt = 0;
       new_tr.filter_cnt = 0;
       ref_hist_tbl[blk_addr] = new_tr;
    }
}

void update_on_miss_trace(size_t blk_addr)
{
    if (tr_hist_tbl.find(blk_addr) != tr_hist_tbl.end() )
    {
       tr_hist_tbl[blk_addr].current_trace = 0;
    }
    else //create new entry
    {
       TraceEntry new_tr;
       new_tr.confidence = false;
       new_tr.current_trace = 0;
       new_tr.old_trace = 0;
       tr_hist_tbl[blk_addr] = new_tr;
    }
}

void update_on_eviction_trace(size_t blk_addr)
{
   //on eviction, update trace
   if (tr_hist_tbl.find(blk_addr) != tr_hist_tbl.end() ) {
     if(tr_hist_tbl[blk_addr].current_trace == tr_hist_tbl[blk_addr].old_trace) {
       tr_hist_tbl[blk_addr].confidence = true;
     } else {
       tr_hist_tbl[blk_addr].confidence = false;
     }
     tr_hist_tbl[blk_addr].old_trace = tr_hist_tbl[blk_addr].current_trace;
     tr_hist_tbl[blk_addr].current_trace = 0;
   }
}

void update_on_eviction_cnt(size_t blk_addr, int ref_cnt)
{
   //on eviction, update trace
   if (ref_hist_tbl.find(blk_addr) != ref_hist_tbl.end() ) {
     if(ref_hist_tbl[blk_addr].dead_cnt == ref_cnt)
     {
       ref_hist_tbl[blk_addr].sat_cnt = 1;
     } else {
       ref_hist_tbl[blk_addr].sat_cnt = 0;
     }
     ref_hist_tbl[blk_addr].dead_cnt = ref_cnt;
   }
}

//update TCP correlation table; use_ref_cnt to differentiate L1 and L2 caches
void update_tc_tbl(TagSR tag_sr, size_t tag, int blk_offs, int set_bits, bool use_ref_cnt)
{
  std::map <size_t, std::list<PredEntry> > & tcp_pred_tbl = (use_ref_cnt) ? l2_tcp_pred_tbl : l1_tcp_pred_tbl;

  if (tag_sr.valid_0 && tag_sr.valid_1 && TcpEnabled )
  {
    size_t tcp_idx = (tag_sr.tag_0 << (64 - blk_offs - set_bits)) | tag_sr.tag_1;
    if (tcp_pred_tbl.find(tcp_idx) == tcp_pred_tbl.end() ) {
       std::list<PredEntry> new_lst;
       PredEntry tcp_entry;
       tcp_entry.tgt_tag = tag;
       tcp_entry.counter = 1;
       new_lst.push_back(tcp_entry);
       tcp_pred_tbl[tcp_idx] = new_lst;
    } else
    {
      std::list<PredEntry> & pred_lst = tcp_pred_tbl.at(tcp_idx);
      bool push_new = true;
      for(std::list<PredEntry>::iterator it = pred_lst.begin(); it != pred_lst.end(); it++)
      {
         if(it->tgt_tag == tag) {
           it->counter += 1;
           push_new = false;
           break;
         }
      }
      //assert(pred_lst.size() == 1);
      PredEntry tcp_entry;
      tcp_entry.tgt_tag = tag;
      tcp_entry.counter = 1;
      if (push_new) pred_lst.push_back(tcp_entry); 
    }
  }
}

//grab TCP-prefetched tag to cache
size_t tcp_prefetch(TagSR tag_sr, int blk_offs, int set_bits, bool use_ref_cnt, bool * did_prefetch)
{
    std::map <size_t, std::list<PredEntry> > & tcp_pred_tbl = (use_ref_cnt) ? l2_tcp_pred_tbl : l1_tcp_pred_tbl;
    unsigned max_cnt = 0;
    size_t prefetch_tag = 0;
    if (tag_sr.valid_0 && tag_sr.valid_1 && TcpEnabled)
    {
      size_t tcp_idx = (tag_sr.tag_0 << (64 - blk_offs - set_bits)) | tag_sr.tag_1;

      if (tcp_pred_tbl.find(tcp_idx) != tcp_pred_tbl.end() )
      {
        std::list<PredEntry> & pred_lst = tcp_pred_tbl.at(tcp_idx);
        for(std::list<PredEntry>::iterator it = pred_lst.begin(); it != pred_lst.end(); it++)
        {
           if(it->counter > max_cnt) {
              max_cnt = it->counter;
              prefetch_tag = it->tgt_tag;
           }
        }
        assert(max_cnt > 0);
      }
    }

    if(max_cnt > 0)
      *did_prefetch = true;

    return prefetch_tag;
}
