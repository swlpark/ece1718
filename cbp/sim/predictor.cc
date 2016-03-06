///////////////////////////////////////////////////////////////////////
////  Copyright 2015 Samsung Austin Semiconductor, LLC.                //
/////////////////////////////////////////////////////////////////////////
//private functions are defined in predictor.h

#include "predictor.h"
#include <iostream>

//Implemented in predictor.h
//PREDICTOR::PREDICTOR()
//{
//    //ECE1718: Your code here (if necessary).
//}
bool PREDICTOR::GetPrediction(UINT64 PC, bool btbANSF,bool btbATSF, bool btbDYN)
{
    //ECE1718: Your code here.
    //compute indices for tagged tables
    for(int i =0; i < t_indices.size(); i++)
      t_indices[i] = tagged_table_index(PC, i);

    find_t_pred(PC);

    if (provider_nomatch)
    {
       alternative_pred = get_b_pred(PC);
    } else {
      alternative_pred = (alternative_nomatch) ? get_b_pred(PC) :
                         (tagged_table[alternative_idx][t_indices[alternative_idx]].pred >= 0);

      if (p_bias < 0 || !weak_pred_counter(tagged_table[alternative_idx][t_indices[alternative_idx]].pred) ||
          tagged_table[alternative_idx][t_indices[alternative_idx]].ubit != 0) {
         return tagged_table[provider_idx][t_indices[provider_idx]].pred >= 0;
      }
    }
    return alternative_pred;
}

//btbANSF (always NT so far)
//btbATSF (always T so far)
//btbDYN (exhibited both NT and T)
void PREDICTOR::UpdatePredictor(UINT64 PC, OpType opType, bool resolveDir, bool predDir, UINT64 branchTarget, bool btbANSF, bool btbATSF, bool btbDYN)
{ 
  //ECE1718: Your code here.
  bool provider_correct = false;
  if (provider_idx < NUM_BANKS)
  {
     bool p_pred = tagged_table[provider_idx][t_indices[provider_idx]].pred >= 0;

     //is the entry recently allocated?
     bool is_recent = (weak_pred_counter(tagged_table[provider_idx][t_indices[provider_idx]].pred) &&
                       (tagged_table[provider_idx][t_indices[provider_idx]].ubit == 0));
     if (is_recent)
     {
        if(resolveDir == p_pred)
          provider_correct = true;
        //altpred and pred differs; p_bias is updated
        if(alternative_pred != p_pred)
        {
          if (alternative_pred == resolveDir)
             sat_count_update(p_bias, true, SAT_U_BOUND);
        }
        else
          sat_count_update(p_bias, false, SAT_L_BOUND);
     }
  }

  //allocate when prediction is incorrect and provider component is not the longest length componenet
  if((predDir != resolveDir) && (provider_idx > 0) && !provider_correct)
    alloc_tagged_entry(PC, resolveDir);

  //update a pred counter
  if(provider_nomatch)
    update_base_table(PC, resolveDir);
  else {
    if (resolveDir)
       sat_count_update(tagged_table[provider_idx][t_indices[provider_idx]].pred, true, SAT_U_BOUND);
    else 
       sat_count_update(tagged_table[provider_idx][t_indices[provider_idx]].pred, false, SAT_L_BOUND);
  }

  //update usefulness if altpred and provider differ
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
