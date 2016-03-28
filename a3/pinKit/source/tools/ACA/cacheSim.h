#ifndef _CACHE_SIM_H_
#define _CACHE_SIM_H_

#include <map>
#include <list>
#include <vector>
#include <iterator>
#include <cassert>
#include <iostream>
#include "dbpAndPrefetch.h"

struct Entry
{
  bool dirty;       //is accessed?
  bool pred_dead;   //predicted dead by DBP
  size_t tag;       //block TAG
  size_t path_hist; //BurstTrace
};

//stores tags of the last two misses
struct TagSR
{
  size_t tag_0;
  size_t tag_1;
  bool valid_0;
  bool valid_1;
  TagSR(): tag_0(0), tag_1(0), valid_0(false), valid_1(false) {}
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

  int rd_cnt;        //number of cache reads
  int wr_cnt;        //number of cache writes
  int cache_miss;    //number of cache misses
  int dbp_cnt;       //num dead-blk predictions
  int dbp_miss_pred; //miss-predicted dead-blks (used for DBP accuracy)
  int evicted_cnt;   //number of evicted blk    (used for DBP coverage)

  cacheSim * parent_cache;

  std::vector< std::list<Entry> > sets; //cache sets
  std::vector< TagSR > miss_hist;       //keeps track of cache misses in each set 

public :
  cacheSim(int, int, int, cacheSim*);

  void access(size_t, size_t, bool);
  int get_access_cnt();
  int get_miss_cnt();
  int get_evicted_cnt();
  int get_dbp_cnt();
  int get_dbp_miss_pred();
};

#endif
