///////////////////////////////////////////////////////////////////////
////  Copyright 2015 Samsung Austin Semiconductor, LLC.                //
/////////////////////////////////////////////////////////////////////////
//private functions are defined in predictor.h

#include "predictor.h"
#include <iostream>

//PREDICTOR::PREDICTOR()
//{
//    //ECE1718: Your code here (if necessary).
//}

bool PREDICTOR::GetPrediction(UINT64 PC, bool btbANSF,bool btbATSF, bool btbDYN)
{
    //ECE1718: Your code here.
    compute_t_indices(PC);
    find_t_pred(PC);
    if (provider_idx == NUM_BANKS)
    {
       alternative_pred = get_b_pred(PC);
       return alternative_pred;
    }
    if (alternative_idx == NUM_BANKS)
       alternative_pred = get_b_pred(PC);
    else
       alternative_pred = (tagged_table[alternative_idx][t_indices[alternative_idx]].ctr >= 0);

      //if the entry is new and counter bias is negative use the alternate prediction
      if (p_bias < 0 || abs(2 * tagged_table[alternative_idx][t_indices[alternative_idx]].ctr + 1) != 1 ||
          tagged_table[alternative_idx][t_indices[alternative_idx]].ubit != 0) {
         return tagged_table[provider_idx][t_indices[provider_idx]].ctr >= 0;
      }
    return alternative_pred;
}

void PREDICTOR::UpdatePredictor(UINT64 PC, OpType opType, bool resolveDir, bool predDir, UINT64 branchTarget, bool btbANSF, bool btbATSF, bool btbDYN)
{ 
  //ECE1718: Your code here.
  //allocate when prediction is incorrect and provider component is not the longest length componenet
  bool new_entry = (predDir != resolveDir) && (provider_idx > 0);

  if (provider_idx < NUM_BANKS)
  {
     bool p_pred = tagged_table[provider_idx][t_indices[provider_idx]].ctr >= 0;
     //is the entry recently allocated?
     bool is_recent = (abs(2 * tagged_table[provider_idx][t_indices[provider_idx]].ctr + 1) == 1) &&
                            (tagged_table[provider_idx][t_indices[provider_idx]].ubit == 0);

     if (is_recent)
     {
        if(resolveDir == p_pred)
          new_entry = false;

        //altpred and pred differs; p_bias is updated
        if(alternative_pred != p_pred) {
          if(alternative_pred == resolveDir) {
             if (p_bias < 7) p_bias += 1;
          } 
        }
        else {
          if (p_bias > -8) p_bias -= 1;
        }

     }
  }
  if(new_entry)
    alloc_tagged_entry(PC, resolveDir);

  //update a pred counter
  if(provider_idx == NUM_BANKS)
    update_base_table(PC, resolveDir);
  else 
    update_ctr(tagged_table[provider_idx][t_indices[provider_idx]].ctr, resolveDir, SAT_BITS);

  if (alternative_pred != predDir && provider_idx < NUM_BANKS)
  {
    if (predDir == resolveDir)
      sat_count_update(tagged_table[provider_idx][t_indices[provider_idx]].ubit, true, 3);
    else
      sat_count_update(tagged_table[provider_idx][t_indices[provider_idx]].ubit, false, 0);
  }
  update_history(PC, resolveDir);
}

void PREDICTOR::TrackOtherInst(UINT64 PC, OpType opType,bool branchDir,UINT64 target)
{ 
    //ECE1718: Your code here.
    //NOT IMPLEMENTED
}

//ECE1718: You must implement this function to return the number of bytes that your
//predictor is using. We will check that it's done honestly.
UINT64 PREDICTOR::GetPredictorSize() 
{ 
  return predictor_size;
}
