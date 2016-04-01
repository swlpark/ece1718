#include "dbpAndPrefetch.h" 

std::map <size_t, TraceEntry> tr_hist_tbl;
std::map <size_t, RefEntry> ref_hist_tbl;
std::map <size_t, std::list<PredEntry> > tcp_pred_tbl;

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
    //tr_hist_tbl[blk_addr].current_trace += (pc & 1) | 1;
    tr_hist_tbl[blk_addr].current_trace += pc;
    tr_hist_tbl[blk_addr].current_trace &= ((1 << 30) - 1);
}

bool predict_db(size_t blk_addr)
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

bool predict_db_ref_cnt(size_t blk_addr)
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

void update_on_miss(size_t blk_addr, size_t pc)
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

void update_on_eviction(size_t blk_addr)
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
