#include "dbpAndPrefetch.h" 

std::map <size_t, TraceEntry> tr_hist_tbl;
std::map <size_t, std::list<PredEntry> > tcp_pred_tbl;
bool TcpEnabled = true;
bool UseCacheBurst = false;
