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
 : rd_cnt(0), wr_cnt(0), cache_miss(0), dbp_cnt(0), dbp_miss_pred(0), evicted_cnt(0), tcp_pr_cnt(0),  
   useless_pr_cnt(0), parent_cache(parent), dbp_use_refcount(false)
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

//Simulates a single cache access   
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

  //sanity check: BLOCK Address Reconstruction
  //size_t blk_addr = (tag_bits << (blk_offs + set_bits)) | (set_idx << blk_offs);
  //assert(blk_addr == (addr & ~((1 << blk_offs) - 1)));

  bool cache_hit = false;
  if(set_idx >= sets.size())
  {
    std::cout << std::dec << "BLK_OFFSET = " << blk_offs << std::endl;
    std::cout << std::dec << "SET_BITS = " << set_bits << std::endl;
    std::cout << std::hex << "TAG_BITS = " << tag_bits << std::endl;
    std::cout << std::dec << "SET_IDX = " << set_idx << std::endl;
    std::cout << std::dec << "NUM SETS = " << sets.size() << std::endl;
    assert(false);
  } 

  //select a cache set
  std::list<Entry> & set = sets.at(set_idx);
  assert(set.size() <= (unsigned) set_ways); 

  //tag and trace of MRU cache block before the cache set is updated
  size_t mru_tag;
  if (set.size() > 0) {
    mru_tag = set.back().tag;
  }
  Entry hit_blk;
  for(std::list<Entry>::iterator it = set.begin(); it != set.end(); it++)
  {
     if (it->tag == tag_bits)
     {
       cache_hit = true;
       it->referenced = true;
       it->dirty = wr_access;
       size_t blk_addr = (tag_bits << (blk_offs + set_bits)) | (set_idx << blk_offs);
 
       //trace update
       if (tag_bits != set.back().tag) 
       {
         update_trace(blk_addr, pc);
       }
       it->refCount += 1;

       //update trace & refCount on a start of BURST
       //see if DBP miss-predicted a blk: the blk is predicted dead, but referenced again!
       if (it->pred_dead) {
         it->pred_dead = false;
         dbp_miss_pred += 1; 
       }
       //else
       //{
       //     if(predict_db(blk_addr))
       //     {
       //       it->pred_dead = true;
       //       dbp_cnt++;
       //     }
       //}
       //LRU position update for the hit block
       hit_blk = *it; 
       set.erase(it);
       set.push_back(hit_blk);
       break;
     }
  } 
  //predict dead block at the end of cache burst
  if(cache_hit && mru_tag != set.back().tag)
  {
    size_t blk_addr = (mru_tag << (blk_offs + set_bits)) | (set_idx << blk_offs);
    if(predict_db(blk_addr))
    {
      //second-last element == last MRU block
      auto it = ++(set.rbegin());
      assert(it->tag == mru_tag);
      it->pred_dead = true;
      dbp_cnt++;
    }
  }

  //access next level cache hiearchy
  if(!cache_hit)
  {
    cache_miss++;
    //start TRACE for missed block
    size_t m_blk_addr = (tag_bits << (blk_offs + set_bits)) | (set_idx << blk_offs);
    update_on_miss(m_blk_addr);

    //1) update TCP correlation table
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
        bool push_new = true;
        for(std::list<PredEntry>::iterator it = pred_lst.begin(); it != pred_lst.end(); it++)
        {
           if(it->tgt_tag == tag_bits) {
             it->counter += 1;
             push_new = false;
             break;
           }
        }

        //assert(pred_lst.size() == 1);
        PredEntry tcp_entry;
        tcp_entry.tgt_tag = tag_bits;
        tcp_entry.counter = 1;
        if (push_new) pred_lst.push_back(tcp_entry); 
      }
    }

    //2) update miss_hist TAG_SR
    tag_sr.tag_0 = tag_sr.tag_1;
    tag_sr.valid_0 = tag_sr.valid_1;
    tag_sr.tag_1 = tag_bits;
    tag_sr.valid_1 = true;
    miss_hist[set_idx] = tag_sr;
   
    //3) Fetch a missed block
    Entry n_blk; 
    n_blk.tag = tag_bits;
    n_blk.dirty = wr_access;
    n_blk.pred_dead = false;
    n_blk.prefetched = false;
    n_blk.referenced = false;
    //n_blk.burstTrace = pc & ((1 << 30) - 1);
    n_blk.refCount = 0;

    if((int)set.size() < set_ways)
    {
      set.push_back(n_blk);
    } 
    else
    {
      //eviction required
      bool is_dirty = set.front().dirty;
      size_t evicted_addr = (set.front().tag << (blk_offs + set_bits)) | (set_idx << blk_offs);
      //size_t evicted_trace = set.front().burstTrace;

      evicted_cnt++;

      //if evicted block is prefetched && never referenced
      if(set.front().referenced == false && set.front().prefetched)
         useless_pr_cnt++;

      set.pop_front();
      set.push_back(n_blk);

      //on eviction, update old_trace
      update_on_eviction(evicted_addr);

      if(is_dirty && parent_cache) 
        parent_cache->access(evicted_addr, pc, true);
    }

    if (parent_cache) 
      parent_cache->access(addr, pc, wr_access);

    //4) Prefetch Operation
    if (tag_sr.valid_0 && tag_sr.valid_1 && TcpEnabled)
    {
      size_t tcp_idx = (tag_sr.tag_0 << (64 - blk_offs - set_bits)) | tag_sr.tag_1;

      if (tcp_pred_tbl.find(tcp_idx) != tcp_pred_tbl.end() ) {

        std::list<PredEntry> & pred_lst = tcp_pred_tbl.at(tcp_idx);
        unsigned max_cnt = 0;
        size_t prefetch_tag = 0;
        for(std::list<PredEntry>::iterator it = pred_lst.begin(); it != pred_lst.end(); it++)
        {
           if(it->counter > max_cnt) {
              max_cnt = it->counter;
              prefetch_tag = it->tgt_tag;
           }
        }
        assert(max_cnt > 0);

        //insert into dead-block position; if not LRU
        //bool use_LRU = true;
        for(std::list<Entry>::iterator it = set.begin(); it != set.end(); it++)
        {
          if (it->pred_dead)
          {
           it->dirty = false;
           it->pred_dead = false;
           it->prefetched = true;
           it->referenced = false;
           it->tag = prefetch_tag;
           it->refCount = 0;
           //use_LRU = false; 
           tcp_pr_cnt++;
          
 
           //DBP update
           size_t blk_addr = (prefetch_tag << (blk_offs + set_bits)) | (set_idx << blk_offs);
           update_on_miss(blk_addr);
           break;
          }
        }

        //insert at LRU position if no dead-block exists in the set
        //if(use_LRU)
        //{
          //Entry p_blk; 
          //p_blk.tag = prefetch_tag;
          //p_blk.dirty = false;
          //p_blk.pred_dead = false;
          //p_blk.prefetched = true;
          //p_blk.referenced = false;
          //p_blk.burstTrace = 0;
          //if((int)set.size() < set_ways) {
          //  set.push_front(p_blk);
          //} else {
          //  set.pop_front();
          //  set.push_front(p_blk);
          //}
        //}
      }
    }
  }
}

//return counters
long cacheSim::get_access_cnt()
{
  return (wr_cnt + rd_cnt);
}

long cacheSim::get_miss_cnt()
{
  return cache_miss;
}

long cacheSim::get_evicted_cnt()
{
  return evicted_cnt;
}

long cacheSim::get_dbp_cnt()
{
  return dbp_cnt;
}

long cacheSim::get_dbp_miss_pred()
{
  return dbp_miss_pred;
}

long cacheSim::get_tcp_pr_cnt()
{
  return tcp_pr_cnt;
}

long cacheSim::get_useless_pr_cnt()
{
  return useless_pr_cnt;
}
