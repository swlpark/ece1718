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

//Trace History Table for L1 cache dead-block prediction
extern std::map <size_t, TraceEntry> tr_hist_tbl;

//TCP: correlation table for TCP access
//access with (TAG, SET_IDX)
extern std::map <size_t, std::list<PredEntry> > tcp_pred_tbl;

extern bool TcpEnabled;
extern bool UseCacheBurst;

//update trace and return true if the block is predicted to be dead
void update_trace (size_t blk_addr, size_t pc);
bool predict_db (size_t blk_addr);
void update_on_miss(size_t blk_addr);
void update_on_eviction(size_t blk_add);

void update_on_miss_ref_cnt(size_t blk_addr, int ref_cnt);

#endif
