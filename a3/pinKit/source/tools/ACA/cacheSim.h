#ifndef _CACHE_SIM_H_
#define _CACHE_SIM_H_

#include <list>
#include <vector>
#include <iterator>
#include <cassert>
#include <iostream>

struct Entry
{
  bool dirty;
  size_t tag;
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

  cacheSim * parent_cache;
  std::vector< std::list<Entry> > sets;

public :
  cacheSim(int, int, int, cacheSim*);

  //size_t address = reinterpret_cast<size_t>(voidptr);
  void access(size_t, bool);
  int get_access_cnt();
  int get_miss_cnt();

};

#endif
