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
 : rd_cnt(0), wr_cnt(0), cache_miss(0), dead_blk_cnt(0), parent_cache(parent)
{
  assert(IS_POW_2(b_sz_b));
  total_size_kb = t_sz_kb;
  block_size_b = b_sz_b;
  set_ways = ways;

  blk_offs = LOGB2C(b_sz_b);

  int num_sets = (total_size_kb * 1024 / block_size_b) / ways;

  set_bits = LOGB2C(num_sets);
  sets.resize(num_sets, std::list<Entry>());
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
       it->path_hist += pc;
       it->path_hist &= ((1 << blk_offs) - 1);

       
       //NOT AT MRU position; check for dead block prediction
       if (tag_bits != set.back().tag) {
         size_t blk_addr = (tag_bits << (blk_offs + set_bits)) | (set_idx << blk_offs);

         if (tr_hist_tbl.find(blk_addr)!= tr_hist_tbl.end()) {
            if(tr_hist_tbl[blk_addr].trace == it->path_hist && tr_hist_tbl[blk_addr].confidence)
              dead_blk_cnt++;
         }
       }

       //LRU position update for the hit block
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
   
    //i.e. query L2 cache for the missing block
    if(parent_cache) 
      parent_cache->access(addr, pc, wr_access);

    Entry n_blk; 
    n_blk.tag = tag_bits;
    n_blk.dirty = wr_access;
    n_blk.path_hist = pc & ((1 << blk_offs) - 1);

    if((int)set.size() < set_ways) {
      set.push_back(n_blk);
    } else
    {
      //eviction required
      bool is_dirty = set.front().dirty;
      size_t evicted_addr = (set.front().tag << (blk_offs + set_bits)) | (set_idx << blk_offs);
      size_t evicted_trace = set.front().path_hist;

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

int cacheSim::get_dead_cnt()
{
  return dead_blk_cnt;
}
