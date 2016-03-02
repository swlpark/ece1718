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
#include "utils.h"

class PREDICTOR{

 public:

  // The interface to the four functions below CAN NOT be changed
  PREDICTOR();
  bool    GetPrediction(UINT64 PC, bool btbANSF, bool btbATSF, bool btbDYN);  
  void    UpdatePredictor(UINT64 PC, OpType opType, bool resolveDir, bool predDir, UINT64 branchTarget, bool btbANSF, bool btbATSF, bool btbDYN);
  void    TrackOtherInst(UINT64 PC, OpType opType, bool branchDir, UINT64 branchTarget);

  //NOTE you are allowed to use btbANFS, btbATSF and btbDYN to filter updates to your predictor or make static predictions if you choose to do so
  
  //ECE1718: You must implement this function to return the number of kB
  //that your predictor is using. We will cbeck that it's done honestly.
  UINT64 GetPredictorSize();
};

#endif

