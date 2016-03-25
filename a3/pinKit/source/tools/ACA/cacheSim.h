#ifndef _CACHE_SIM_H_
#define _CACHE_SIM_H_

#include <list>
#include <vector>
#include <iterator>
#include <iterator>

struct cacheEntry
{

}

class cacheSim 
{
  //cache
  int total_size_kb;
  int block_size_b;
  int set_ways;

  cacheSim * parent_cache;

  std::vector<std::list<cacheEntry>>
  cacheSim(int, int, int)

public :
  //size_t address = reinterpret_cast<size_t>(voidptr);
  bool cache_access(size_t addr);

};

#endif
