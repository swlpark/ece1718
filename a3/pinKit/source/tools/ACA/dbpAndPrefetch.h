#ifndef _DBP_H_
#define _DBP_H_

#include <map>
#include <list>

//TraceEntry: used for dead-block prediction for L1 cache
struct TraceEntry
{
  bool confidence;  
  size_t trace;
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

#endif
