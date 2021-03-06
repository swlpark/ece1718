///////////////////////////////////////////////////////////////////////
////  Copyright 2015 Samsung Austin Semiconductor, LLC.                //
/////////////////////////////////////////////////////////////////////////
//
#ifndef _PREDICTOR_H_
#define _PREDICTOR_H_

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <bitset>
#include <assert.h>
#include <vector>
#include <iterator>
#include "utils.h"

//Paramemters for 5-component TAGE tables
#define LOG_BASE   13
#define BASE_T_SIZE    (1 << LOG_BASE)
#define LOG_TAGGED 13
#define TAGGED_T_SIZE  (1 << LOG_TAGGED)
#define NUM_BANKS 4

//saturating pred cnt length
#define SAT_BITS 3
#define SAT_L_BOUND -(1 << SAT_BITS) 
#define SAT_U_BOUND ((1 << SAT_BITS) - 1)

//saturating pred cnt length
#define U_BITS 2
#define TAG_BITS 10

#define MAX_HIST_LEN 128
#define MIN_HIST_LEN 4

//truncate vector by bit masking
#define TRUNCATE(VECTOR,SIZE)   VECTOR & ((1 << SIZE) - 1)

//history type
typedef bitset<MAX_HIST_LEN> history_t;

//base component = simple bimodal prediction
struct b_entry {
  int pred;
  b_entry() : pred(0) {}
};

//tagged component = indexed with increasing history lengths; usef
struct t_entry {
  int pred;
  int tag;
  int ubit; //usefulness count
  t_entry() : pred(0), tag(0), ubit(0) {}
};

//folded history as described by PMM paper; 
struct folded_history
{
  unsigned folded; //folded history
  unsigned c_length; //compression length
  unsigned o_length; //original history length
  unsigned m_length; //mod length; trailing bits after folding
  void update(history_t h)
  {
     folded = (folded << 1) | h[0];
     folded ^= h[o_length] << m_length;
     folded ^= (folded >> c_length);
     folded = TRUNCATE(folded, c_length);
  }
  void setup(int orig_len, int com_len)
  {
     assert(orig_len >= 0);
     assert(com_len >= 0);
     folded = 0;
     o_length = orig_len;
     c_length = com_len;
     m_length = o_length % c_length;

     assert(o_length < MAX_HIST_LEN);
  }
};

class PREDICTOR{
 //prediction tables and folded history vectors
 std::vector<b_entry> base_table;
 std::vector<folded_history> hist_i;
 std::vector<folded_history> hist_t0;
 std::vector<folded_history> hist_t1;
 t_entry tagged_table[NUM_BANKS][TAGGED_T_SIZE];

 //geometric path history bits (i.e. h[0:L(i)] in TAGE paper)
 std::vector<int> idx_lengths;
 //indices to tagged tables for a given PC
 std::vector<int> t_indices;

 //global branch history shift register
 history_t global_history; 

 //encodes an executed path in a 10-bit vector
 int path_history;

 //table tag matches set by find_t_pred() function
 int provider_idx, alternative_idx;
 bool provider_pred, alternative_pred;
 bool provider_nomatch, alternative_nomatch;

 //Branch Predictor SIZE
 int predictor_size;

 //bits to determine if new entries should be considered as valid or not for prediction
 int p_bias;

 void sat_count_update(int & cnt, bool incr, int bound)
 {
   if(incr) {
     if(cnt < bound) cnt++;
   } else {
     if(cnt > bound) cnt--;
   }
 }
 
 int base_table_index(UINT64 PC)
 {
   return TRUNCATE(PC, LOG_BASE);
 }

 static bool weak_pred_counter(int cnt)
 {
   return (cnt == 0 || cnt == -1);
 }

 //hash path history info
 int _path_hist_hash(int hist, int size, int bank)
 {
    int temp1, temp2;
    hist = TRUNCATE(hist, size); 
    temp1 = TRUNCATE(hist, LOG_TAGGED); 
    temp2 = (hist >> LOG_TAGGED);
    temp2 = ((temp2 << bank) & ((1 << LOG_TAGGED) - 1)) + (temp2 >> (LOG_TAGGED - bank));
    hist = temp1 ^ temp2;
    hist = ((hist << bank) & ((1 << LOG_TAGGED) - 1)) + (hist >> (LOG_TAGGED - bank));
    return hist;
 }

 //get index for the tagged tables; include path history as in the OGHEL predictor
 int tagged_table_index (UINT64 PC, int bank)
 {
   assert(bank < NUM_BANKS);
   int idx = PC ^ (PC >> ((LOG_TAGGED - (NUM_BANKS - bank - 1)))) ^ hist_i[bank].folded;
   int p_hist_length = (idx_lengths[bank] >= LOG_TAGGED) ? LOG_TAGGED : idx_lengths[bank];
   idx ^= _path_hist_hash(path_history, p_hist_length, bank);

   //truncate the hashed idx
   return TRUNCATE(idx, LOG_TAGGED);
 }

 //update saturating counter in the base table;
 //taken if 0, 1; not taken if -2, -1
 void update_base_table(UINT64 PC, bool br_taken)
 {
   int idx = base_table_index(PC);
   if(br_taken) {
     if(base_table[idx].pred < 1)
       base_table[idx].pred++;
   } else {
     if(base_table[idx].pred > -2)
       base_table[idx].pred--;
   }
 }

 //compute tag
 int compute_tag(UINT64 PC, int bank)
 {
   int tag = PC ^ hist_t0[bank].folded ^ (hist_t1[bank].folded << 1);
   //truncate with variable tag lengths for different tables
   tag = TRUNCATE(tag, TAG_BITS);
   return tag;
 }

 //try to allocate a new entry if pred. is wrong
 void alloc_tagged_entry (UINT64 PC, bool br_taken)
 {
   //int min_u = 3;
   int min_u = NUM_BANKS - 1;
   int min_idx = 0;

   //find the entry with the lowest usefulness count 
   for (int i = 0; i < provider_idx; i++)
   {
     if (tagged_table[i][t_indices[i]].ubit < min_u) {
       min_u = tagged_table[i][t_indices[i]].ubit;
       min_idx = i;
     }
   }

   //no entry with zero usefulness counter; decrement u for matching entries
   if (min_u > 0) {
     for (int i = 0; i < provider_idx; i++)
        tagged_table[i][t_indices[i]].ubit -= 1;
   } else {
     //allocate new component entry 
     if (br_taken)
       tagged_table[min_idx][t_indices[min_idx]].pred = 0;
     else
       tagged_table[min_idx][t_indices[min_idx]].pred = -1;
     tagged_table[min_idx][t_indices[min_idx]].tag = compute_tag(PC, min_idx);
     tagged_table[min_idx][t_indices[min_idx]].ubit = 0;
   }
   
 }

 void update_history(UINT64 PC, bool br_taken)
 {
   //update path history
   //path_history = (path_history << 1) + (PC & 1);
   path_history = (path_history << 1);
   path_history += ((PC & 2) == 2) ? 1 : 0;
   path_history = TRUNCATE(path_history, LOG_TAGGED << 1);

   //update global history
   global_history = global_history << 1;
   global_history |= (br_taken) ? (history_t) 1 : (history_t) 0; 

   //update tag & index folded history tables
   for(int i = 0; i < NUM_BANKS; i++)
   {
     hist_t0[i].update(global_history);
     hist_t1[i].update(global_history);
     hist_i[i].update(global_history);
   }
 }

 void find_t_pred(UINT64 PC)
 {
    provider_idx = NUM_BANKS;
    alternative_idx = NUM_BANKS;
    for(int i =0; i < NUM_BANKS; i++)
    {

      if (tagged_table[i][t_indices[i]].tag == compute_tag(PC, i))
      {
        provider_idx = i;
        break;
      }
    }
    for(int i = provider_idx + 1; i < NUM_BANKS; i++)
    {
      if (tagged_table[i][t_indices[i]].tag == compute_tag(PC, i))
      {
        alternative_idx = i;
        break;
      }
    }
    provider_nomatch = (provider_idx == NUM_BANKS);
    alternative_nomatch = (alternative_idx == NUM_BANKS);
 }

 bool get_b_pred(UINT64 PC)
 {
    return base_table[base_table_index(PC)].pred >= 0;
 }

 public:

  // The interface to the four functions below CAN NOT be changed
  PREDICTOR() : base_table(BASE_T_SIZE), hist_i(NUM_BANKS), hist_t0(NUM_BANKS), hist_t1(NUM_BANKS),
                t_indices(NUM_BANKS), idx_lengths(NUM_BANKS)
  {
     std::cout << "Geometric History Lengths: \n";
     idx_lengths[0] = MAX_HIST_LEN- 1;      
     std::cout << "L[0]: " << idx_lengths[0] << std::endl;

     //set up geometric history lengths for each tagged table
     //the longest history length is at L[0] = MAX_HIST -1
     //the shortest history length is at L[NUM-BANKS-1] = MIN_HIST
     //not exactly geometric, but that's okay
     for(int i = 1; i < NUM_BANKS - 1; i+=1)
     {
        int idx = NUM_BANKS - i - 1;
        double tmp = pow((double)(MAX_HIST_LEN) / (double) MIN_HIST_LEN, (double)i / (double)(NUM_BANKS - 1));
        idx_lengths[idx] = ceil(MIN_HIST_LEN * tmp);
        std::cout << "L[" << idx << "]: " << idx_lengths[idx] << std::endl;
     }
     idx_lengths[NUM_BANKS - 1] = MIN_HIST_LEN;
     std::cout << "L[" << NUM_BANKS - 1 << "]: " << idx_lengths[NUM_BANKS - 1] << std::endl;

     p_bias = 0;
     //compute total storage size of tables; start with base pred table
     predictor_size = BASE_T_SIZE * SAT_BITS;

     //initialize tagged tables
     for(int i = 0; i < NUM_BANKS; i+=1)
     {
       hist_i[i].setup(idx_lengths[i], LOG_TAGGED);
       hist_t0[i].setup(idx_lengths[i], TAG_BITS);
       hist_t1[i].setup(idx_lengths[i], TAG_BITS - 1);
       predictor_size += TAGGED_T_SIZE * (SAT_BITS + U_BITS + TAG_BITS);
     }
     int size_in_KB = (predictor_size / 8);
     size_in_KB = (size_in_KB / 1024);

     std::cout << "Predictor table size = " << size_in_KB << " KB \n";
  }

  bool GetPrediction(UINT64 PC, bool btbANSF, bool btbATSF, bool btbDYN);
  void UpdatePredictor(UINT64 PC, OpType opType, bool resolveDir, bool predDir, UINT64 branchTarget, bool btbANSF, bool btbATSF, bool btbDYN);
  void    TrackOtherInst(UINT64 PC, OpType opType, bool branchDir, UINT64 branchTarget);

  //NOTE you are allowed to use btbANFS, btbATSF and btbDYN to filter updates to your predictor or make static predictions if you choose to do so
  //ECE1718: You must implement this function to return the number of kB
  //that your predictor is using. We will cbeck that it's done honestly.
  UINT64 GetPredictorSize();
};

#endif

