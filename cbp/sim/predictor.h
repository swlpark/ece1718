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
#include "utils.h"

//Paramemters for 64KB, 5-component TAGE tables
#define LOG_BASE   13
#define BASE_T_SIZE    (1 << LOG_BASE)
#define LOG_TAGGED 10
#define TAGGED_T_SIZE  (1 << LOG_TAGGED)
#define NUM_BANKS 4
#define SAT_BITS 3
#define TAG_BITS 11

#define MAX_HIST_LEN 131
#define MIN_HIST_LEN 3

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
  int c_length; //compression length
  int o_length; //original history length
  int m_length; //mod length; trailing bits after folding

  void update(history_t h)
  {
     folded = (folded << 1) | h[0];
     folded ^= h[o_length] << m_length;
     folded ^= (folded >> c_length);
     folded = TRUNCATE(folded, c_length);
  }

  void setup(int orig_len, int com_len)
  {
     folded = 0;
     o_length = orig_len;
     c_length = com_len;
     m_length = o_length % c_length;
     assert(o_length < MAX_HIST_LEN);
  }

};

class PREDICTOR{
 b_entry base_table  [BASE_T_SIZE];
 t_entry tagged_table[NUM_BANKS][TAGGED_T_SIZE];

 history_t global_history; 
 //folded history tables for index and tag computation
 folded_history hist_i[NUM_BANKS];
 folded_history hist_t[2][NUM_BANKS];

 //geometric path history bits (i.e. h[0:L(i)] in TAGE paper)
 int idx_lengths[NUM_BANKS];

 //indices to tagged tables for a given PC
 int t_indices[NUM_BANKS];

 //encodes an executed path in a 10-bit vector
 int path_history;
 int provider_idx, alternative_idx;
 bool provider_pred, alternative_pred;

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

 int _path_hist_hash(int hist, int size, int bank)
 {
    int temp1, temp2;
    //hist = hist & ((1 << size) - 1);
    hist = TRUNCATE(hist, size); 
    //temp1 = (hist & ((1 << LOG_TAGGED) - 1));
    temp1 = TRUNCATE(hist, LOG_TAGGED); 
    temp2 = (hist >> LOG_TAGGED);
    temp2 = ((temp2 << bank) & ((1 << LOG_TAGGED) - 1)) + (temp2 >> (LOG_TAGGED - bank));
    //temp2 = TRUNCATE((temp2 << bank), LOG_TAGGED) + (temp2 >> (LOG_TAGGED - bank));

    hist = temp1 ^ temp2;
    hist = ((hist << bank) & ((1 << LOG_TAGGED) - 1)) + (hist >> (LOG_TAGGED - bank));
    //hist = TRUNCATE((hist << bank), LOG_TAGGED) + (hist >> (LOG_TAGGED - bank));

    return hist;
 }

 //get index for the tagged tables; include path history as in the OGHEL predictor
 int tagged_table_index (UINT64 PC, int bank)
 {
   assert(bank < NUM_BANKS);
   int idx = PC ^ (PC >> ((LOG_TAGGED - (NUM_BANKS - bank - 1)))) ^ hist_i[bank].folded;

   //cap path history mixing at length 16
   int p_hist_length = (idx_lengths[bank] >= 16) ? 16 : idx_lengths[bank];
   idx ^= _path_hist_hash(path_history, p_hist_length, bank);

   //truncate
   //retval = retval & ((1 << LOG_TAGGED) - 1);
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

 //update pred counter in a tagged tables
 void update_pred(int & pred, bool br_taken, int size)
 {
   if(br_taken) {
      if (pred < ((1 << (size - 1)) - 1))
         pred++;
   }
   else {
      if (pred > -(1 << (size - 1)))
         pred--;
   }
 }

 //compute tag
 int compute_tag(UINT64 PC, int bank)
 {
   int tag = PC ^ hist_t[0][bank].folded ^ (hist_t[1][bank].folded << 1);
   //truncate with variable tag lengths for different tables
   tag = TRUNCATE(tag, (TAG_BITS - ((bank + (NUM_BANKS & 1)) / 2)));
   return tag;
 }

 //try to allocate a new entry if pred. is wrong
 void alloc_tagged_entry (UINT64 PC, bool br_taken)
 {
   int min_u = 3;
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
   path_history = (path_history << 1) + (PC & 1);
   path_history = TRUNCATE(path_history, 10);

   //update global history
   global_history = global_history << 1;
   global_history |= (br_taken) ? (history_t) 1 : (history_t) 0; 

   //update tag & index folded history tables
   for(int i = 0; i < NUM_BANKS; i++)
   {
     hist_t[0][i].update(global_history);
     hist_t[1][i].update(global_history);
     hist_i[i].update(global_history);
   }
 }

 //fill in t_indices table for tagged table access 
 void compute_t_indices(UINT64 PC)
 {
    for(int i =0; i < NUM_BANKS; i++)
    {
      t_indices[i] = tagged_table_index(PC, i);
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
 }

 bool get_b_pred(UINT64 PC)
 {
    return base_table[base_table_index(PC)].pred >= 0;
 }

 public:

  // The interface to the four functions below CAN NOT be changed
  PREDICTOR()
  {
     std::cout << "Geometric History Lengths: \n";
     idx_lengths[0] = MAX_HIST_LEN - 1;      
     std::cout << "L[0]: " << idx_lengths[0] << std::endl;

     //set up geometric history lengths for each tagged table
     for(int i = 1; i < NUM_BANKS - 1; i+=1)
     {
        int idx = NUM_BANKS - i - 1;
        double tmp = pow((double)(MAX_HIST_LEN - 1) / MIN_HIST_LEN, (double)i / (NUM_BANKS - 1));
        idx_lengths[idx] = (int) (MIN_HIST_LEN * tmp + 0.5);
        std::cout << "L[" << idx << "]: " << idx_lengths[idx] << std::endl;
     }
     idx_lengths[NUM_BANKS - 1] = MIN_HIST_LEN;
     std::cout << "L[" << NUM_BANKS - 1 << "]: " << idx_lengths[NUM_BANKS - 1] << std::endl;

     p_bias = 0;
     predictor_size = 0;

     //initialize tagged tables
     for(int i = 0; i < NUM_BANKS; i+=1)
     {
       hist_i[i].setup(idx_lengths[i], LOG_TAGGED);
       hist_t[0][i].setup(idx_lengths[i], TAG_BITS - ((i + (NUM_BANKS & 1)) / 2));
       hist_t[1][i].setup(idx_lengths[i], TAG_BITS - ((i + (NUM_BANKS & 1)) / 2) - 1);
       predictor_size += (1 << LOG_TAGGED) * (5 + TAG_BITS - ((i + (NUM_BANKS & 1)) / 2));
     }
     std::cout << "Predictor table size = " << predictor_size << " B\n";
  }
  //btbANSF (always NT so far)
  //btbATSF (always T so far)
  //btbDYN (exhibited both NT and T)
  bool GetPrediction(UINT64 PC, bool btbANSF, bool btbATSF, bool btbDYN);
  void UpdatePredictor(UINT64 PC, OpType opType, bool resolveDir, bool predDir, UINT64 branchTarget, bool btbANSF, bool btbATSF, bool btbDYN);
  void    TrackOtherInst(UINT64 PC, OpType opType, bool branchDir, UINT64 branchTarget);

  //NOTE you are allowed to use btbANFS, btbATSF and btbDYN to filter updates to your predictor or make static predictions if you choose to do so
  //ECE1718: You must implement this function to return the number of kB
  //that your predictor is using. We will cbeck that it's done honestly.
  UINT64 GetPredictorSize();
};

#endif

