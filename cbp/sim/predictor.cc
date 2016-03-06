///////////////////////////////////////////////////////////////////////
////  Copyright 2015 Samsung Austin Semiconductor, LLC.                //
/////////////////////////////////////////////////////////////////////////

//I opted to implement predictor in the header file itself

//#include "predictor.h"
//#include <iostream>
//
//PREDICTOR::PREDICTOR()
//{
//    //ECE1718: Your code here (if necessary).
//}
//
//bool PREDICTOR::GetPrediction(UINT64 pc,bool btbANSF,bool btbATSF, bool btbDYN)
//{
//    //ECE1718: Your code here.
//    return TAKEN;
//}
//
//void PREDICTOR::UpdatePredictor(UINT64 pc,OpType opType, bool resolveDir, bool predDir, UINT64 branchTarget, bool btbANSF, bool btbATSF, bool btbDYN)
//{ 
//    //ECE1718: Your code here.
//}
//
//void PREDICTOR::TrackOtherInst(UINT64 pc,OpType opType,bool branchDir,UINT64 target)
//{ 
//    //ECE1718: Your code here.
//}
//
////ECE1718: You must implement this function to return the number of bytes that your
////predictor is using. We will check that it's done honestly.
//UINT64 PREDICTOR::GetPredictorSize() 
//{ 
//    return 0; 
//}
