#ifndef _CACHE_SIM_H_
#define _CACHE_SIM_H_

#include <map>
#include <list>
#include <vector>
#include <iterator>
#include <cassert>
#include <iostream>
#include "dbpAndPrefetch.h"

//cache block entry struct
struct Entry
{
  bool dirty;       //is accessed?
  bool pred_dead;   //predicted dead by DBP
  bool prefetched;  //prefetched blk by TCP
  bool referenced;  //is this block ever referenced?
  size_t tag;       //block TAG
  unsigned refCount;//reference count
};


class cacheSim 
{
  //cache size params
  int total_size_kb;
  int block_size_b;
  int set_ways;

  //address bit widths
  int set_bits;
  int blk_offs;

  long rd_cnt;        // number of cache reads
  long wr_cnt;        // number of cache writes
  long cache_miss;    // number of cache misses
  long dbp_cnt;       // num dead-blk predictions
  long dbp_miss_pred; // miss-predicted dead-blks (used for DBP accuracy)
  long evicted_cnt;   // number of evicted blk    (used for DBP coverage)

  long tcp_pr_cnt;     // number of blocks prefetched by TCP
  long useless_pr_cnt; // prefetches that are not referenced

  cacheSim * parent_cache;

  std::vector< std::list<Entry> > sets; //cache sets
  std::vector< TagSR > miss_hist;       //keeps track of cache misses in each set 

public :
  //L1: false (uses burstTrace)
  //L2: true  (uses refCount+)
  bool dbp_use_refcount;

  cacheSim(int, int, int, cacheSim*);

  void access(size_t, size_t, bool);
  long get_access_cnt();
  long get_miss_cnt();
  long get_evicted_cnt();
  long get_dbp_cnt();
  long get_dbp_miss_pred();

  long get_tcp_pr_cnt();
  long get_useless_pr_cnt();
};

#endif
