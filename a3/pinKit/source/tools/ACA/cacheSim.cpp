#include "cacheSim.h"

static inline bool IS_POW_2(int num)
{
  return ((num & (num - 1)) == 0);
}

static inline int LOGB2C(int num)
{
  num -= 1;
  assert(num > 0); 
  int retval = 0;
  while (num != 0) {
    num >>= 1;
    ++retval;
  }
  return retval;
}

cacheSim::cacheSim(int t_sz_kb, int b_sz_b, int ways, cacheSim* parent)
 : rd_cnt(0), wr_cnt(0), cache_miss(0), dbp_cnt(0), dbp_miss_pred(0), evicted_cnt(0), parent_cache(parent)
{
  assert(IS_POW_2(b_sz_b));
  total_size_kb = t_sz_kb;
  block_size_b = b_sz_b;
  set_ways = ways;

  blk_offs = LOGB2C(b_sz_b);

  int num_sets = (total_size_kb * 1024 / block_size_b) / ways;

  set_bits = LOGB2C(num_sets);
  sets.resize(num_sets, std::list<Entry>());
  miss_hist.resize(num_sets, TagSR());
}

void cacheSim::access(size_t addr, size_t pc, bool wr_access)
{
  if (wr_access)
  {
    wr_cnt++;
  }
  else
  {
    rd_cnt++;
  }

  size_t set_idx = (addr >> blk_offs) & ((1 << set_bits) - 1);
  size_t tag_bits = addr >> (blk_offs + set_bits);

  bool cache_hit = false;
  if(set_idx >= sets.size()) {
    std::cout << std::dec << "BLK_OFFSET = " << blk_offs << std::endl;
    std::cout << std::dec << "SET_BITS = " << set_bits << std::endl;
    std::cout << std::hex << "TAG_BITS = " << tag_bits << std::endl;
    std::cout << std::dec << "SET_IDX = " << set_idx << std::endl;
    std::cout << std::dec << "NUM SETS = " << sets.size() << std::endl;
    assert(false);
  } 

  std::list<Entry> & set = sets.at(set_idx);
  assert(set.size() <= (unsigned) set_ways); 

  Entry hit_blk;
  for(std::list<Entry>::iterator it = set.begin(); it != set.end(); it++)
  {
     if (it->tag == tag_bits) {
       cache_hit = true;
       it->dirty = wr_access;
       //it->path_hist ^= pc;
       //it->path_hist &= ((1 << blk_offs) - 1);
       it->path_hist += 1;

       size_t blk_addr = (tag_bits << (blk_offs + set_bits)) | (set_idx << blk_offs);

       //LRU position update for the hit block
       //DBP missprediction: the blk is predicted dead, but referenced again!
       if(it->pred_dead) {
         tr_hist_tbl[blk_addr].confidence = false;
         it->pred_dead = false;
         dbp_miss_pred += 1; 
       }

       //NOT AT MRU position; check for dead block prediction
       if (tag_bits != set.back().tag || !UseCacheBurst) {
         if (tr_hist_tbl.find(blk_addr)!= tr_hist_tbl.end()) {
            if(tr_hist_tbl[blk_addr].trace == it->path_hist && tr_hist_tbl[blk_addr].confidence) {
              it->pred_dead = true;
              dbp_cnt++;
            }
         }
       }

       hit_blk = *it; 
       set.erase(it);
       set.push_back(hit_blk);
       break;
     }
  } 

  //access next level cache hiearchy
  if(!cache_hit)
  {
    cache_miss++;

    //1) update TCP correlation  table
    TagSR tag_sr = miss_hist.at(set_idx); 
    if (tag_sr.valid_0 && tag_sr.valid_1 && TcpEnabled ) {
      size_t tcp_idx = (tag_sr.tag_0 << (64 - blk_offs - set_bits)) | tag_sr.tag_1;

      if (tcp_pred_tbl.find(tcp_idx) == tcp_pred_tbl.end() ) {
         std::list<PredEntry> new_lst;
         PredEntry tcp_entry;
         tcp_entry.tgt_tag = tag_bits;
         tcp_entry.counter = 1;
         new_lst.push_back(tcp_entry);
         tcp_pred_tbl[tcp_idx] = new_lst;
      } else
      {
        std::list<PredEntry> & pred_lst = tcp_pred_tbl.at(tcp_idx);
        for(std::list<PredEntry>::iterator it = pred_lst.begin(); it != pred_lst.end(); it++)
        {
           if(it->tgt_tag == tag_bits) {
             it->counter += 1;
             break;
           }
        }
      }
    }

    //2) update miss_hist TAG_SR
    tag_sr.tag_0 = tag_sr.tag_1;
    tag_sr.valid_0 = tag_sr.valid_1;
    tag_sr.tag_1 = tag_bits;
    tag_sr.valid_1 = true;
    miss_hist[set_idx] = tag_sr;
   
    //3) Prefetch Operation
    if (tag_sr.valid_0 && tag_sr.valid_1 && TcpEnabled ) {
      size_t tcp_idx = (tag_sr.tag_0 << (64 - blk_offs - set_bits)) | tag_sr.tag_1;

      if (tcp_pred_tbl.find(tcp_idx) != tcp_pred_tbl.end() ) {
        std::list<PredEntry> & pred_lst = tcp_pred_tbl.at(tcp_idx);
        unsigned max_cnt = 0;
        size_t prefetch_addr = 0;
        for(std::list<PredEntry>::iterator it = pred_lst.begin(); it != pred_lst.end(); it++)
        {
           if(it->counter > max_cnt) {
              max_cnt = it->counter;
              prefetch_addr = (it->tgt_tag << (blk_offs + set_bits)) | (set_idx << blk_offs);
           }
        }
        assert(prefetch_addr > 0);
      }
    }

    //i.e. query L2 cache for the missing block
    if(parent_cache) 
      parent_cache->access(addr, pc, wr_access);

    Entry n_blk; 
    n_blk.tag = tag_bits;
    n_blk.dirty = wr_access;
    n_blk.pred_dead = false;
    //n_blk.path_hist = pc & ((1 << blk_offs) - 1);

    //using counter instead
    n_blk.path_hist = 0;

    if((int)set.size() < set_ways) {
      set.push_back(n_blk);
    } else
    {
      //eviction required
      bool is_dirty = set.front().dirty;
      size_t evicted_addr = (set.front().tag << (blk_offs + set_bits)) | (set_idx << blk_offs);
      size_t evicted_trace = set.front().path_hist;

      evicted_cnt++;
      set.pop_front();
      set.push_back(n_blk);

      if (tr_hist_tbl.find(evicted_addr) == tr_hist_tbl.end() ) {
        TraceEntry new_tr;
        new_tr.confidence = false;
        new_tr.trace = evicted_trace;
        tr_hist_tbl[evicted_addr] = new_tr;

      } else {
        if(tr_hist_tbl[evicted_addr].trace == evicted_trace) {
          tr_hist_tbl[evicted_addr].confidence = true;
        } else {
          tr_hist_tbl[evicted_addr].confidence = false;
        }
      }

      if(is_dirty && parent_cache) 
        parent_cache->access(evicted_addr, pc, true);
    }
  }
  //std::cerr << "ADDR: " << std::hex << addr << "; SET_IDX: " << std::dec << set_idx << std::endl;
}

int cacheSim::get_access_cnt()
{
  return (wr_cnt + rd_cnt);
}

int cacheSim::get_miss_cnt()
{
  return cache_miss;
}

int cacheSim::get_evicted_cnt()
{
  return evicted_cnt;
}

int cacheSim::get_dbp_cnt()
{
  return dbp_cnt;
}

int cacheSim::get_dbp_miss_pred()
{
  return dbp_miss_pred;
}


