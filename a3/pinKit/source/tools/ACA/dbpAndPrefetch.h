#ifndef _DBP_H_
#define _DBP_H_

#include <map>
#include <list>
#include <cassert>
#include <iostream>

//TraceEntry: used for dead-block prediction for L1 cache
struct TraceEntry
{
  bool confidence;  
  size_t current_trace;
  size_t old_trace;
};

//RefEntry: used for dead-block prediction for L2 cache
struct RefEntry
{
  int sat_cnt;  //saturating count (0-3)
  int dead_cnt; //threshold count
  int filter_cnt; //hold smaller cnt
};

//TCP prediction entry
struct PredEntry
{
  unsigned counter;  
  size_t   tgt_tag;
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


//BurstTrace History Table for L1 cache dead-block prediction
extern std::map <size_t, TraceEntry> tr_hist_tbl;

//RefCount+ History Table for L1 cache dead-block prediction
extern std::map <size_t, RefEntry> ref_hist_tbl;

//TCP: correlation table for TCP access
extern std::map <size_t, std::list<PredEntry> > tcp_pred_tbl;

extern bool TcpEnabled;
extern bool UseCacheBurst;

//update trace and return true if the block is predicted to be dead
void update_trace (size_t blk_addr, size_t pc);
bool predict_db_trace (size_t blk_addr);
bool predict_db_cnt(size_t blk_addr, int ref_cnt);
void insert_on_miss_trace(size_t blk_addr, size_t pc);
void insert_on_miss_cnt(size_t blk_addr);
void update_on_eviction_trace(size_t blk_add);
void update_on_eviction_cnt(size_t blk_add, int ref_cnt);

void update_tc_tbl(TagSR tag_sr, size_t tag, int blk_offs, int set_bits, bool use_ref_cnt);
size_t tcp_prefetch(TagSR tag_sr, int blk_offs, int set_bits, bool use_ref_cnt, bool * prefetched);

#endif
