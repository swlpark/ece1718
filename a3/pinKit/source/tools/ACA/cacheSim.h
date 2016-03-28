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
  bool dirty;
  size_t tag;
  size_t path_hist;
};

struct TagSR
{
  size_t tag_0;
  size_t tag_1;
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

  int rd_cnt;
  int wr_cnt;
  int cache_miss;
  int dead_blk_cnt;

  cacheSim * parent_cache;
  std::vector< std::list<Entry> > sets;
  std::vector< TagSR > miss_hist;

public :
  cacheSim(int, int, int, cacheSim*);

  void access(size_t, size_t, bool);
  int get_access_cnt();
  int get_miss_cnt();
  int get_dead_cnt();

};

#endif
