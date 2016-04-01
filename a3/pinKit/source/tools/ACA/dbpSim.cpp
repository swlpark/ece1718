/*BEGIN_LEGAL 
Intel Open Source License 

Copyright (c) 2002-2013 Intel Corporation. All rights reserved.
 
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.  Redistributions
in binary form must reproduce the above copyright notice, this list of
conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.  Neither the name of
the Intel Corporation nor the names of its contributors may be used to
endorse or promote products derived from this software without
specific prior written permission.
 
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE INTEL OR
ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
END_LEGAL */
/* ===================================================================== */
/*
  @ORIGINAL_AUTHOR: Robert Cohn
*/

/* ===================================================================== */
/*! @file
 *  This file contains an ISA-portable PIN tool for tracing memory accesses.
 */

// MODIFIED: March 2016, for University of Toronto advanced computer 
// architecture.
// - markj sutherland
#include "pin.H"
#include "cacheSim.h"
#include "gzstream.hpp"
#include "dbpAndPrefetch.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <map>

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */
gz::ogzstream TraceFile;
static UINT64 instCount = 0;

cacheSim * L1_I_CACHE;
cacheSim * L1_D_CACHE;
cacheSim * L2_CACHE;

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */
KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool",
    "o", "pinMemTrace.txt.gz", "specify trace file name");
KNOB<BOOL> KnobValues(KNOB_MODE_WRITEONCE, "pintool",
    "values", "1", "Output memory values reads and written");

KNOB<int> L1_cache_total_kb(KNOB_MODE_WRITEONCE, "pintool", "l1s", "64", "set L1 cache total size in KB");
KNOB<int> L1_cache_block_b(KNOB_MODE_WRITEONCE, "pintool", "l1b", "64", "set L1 cache block size in Bytes");
KNOB<int> L1_cache_assoc_w(KNOB_MODE_WRITEONCE, "pintool", "l1w", "4", "set L1 cache ways");

KNOB<int> L2_cache_total_kb(KNOB_MODE_WRITEONCE, "pintool", "l2s", "1024", "set L2 cache total size in KB");
KNOB<int> L2_cache_block_b(KNOB_MODE_WRITEONCE, "pintool", "l2b", "64", "set L2 cache block size in Bytes");
KNOB<int> L2_cache_assoc_w(KNOB_MODE_WRITEONCE, "pintool", "l2w", "16", "set L2 cache ways");

KNOB<int> tcp_enable(KNOB_MODE_WRITEONCE, "pintool", "p", "false", "TCP Prefetcher Enable = 1 Disable= 0");

/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */

static INT32 Usage()
{
    cerr <<
        "This tool produces a memory address trace.\n"
        "For each (dynamic) instruction reading or writing to memory the the ip is recorded\n"
        "\n";
    cerr << KNOB_BASE::StringKnobSummary();
    cerr << endl;
    return -1;
}

//L2 Data Cache Access - Read 
static VOID RecordMemRead(VOID * ip, VOID * addr)
{
    L1_D_CACHE->access((size_t)addr, (size_t)ip, false);
#ifdef _DEBUG_
    TraceFile << "@ " << dec << instCount << ", " << hex << ip << ": " << addr << " READ\n" ;
#endif
}

static VOID * WriteAddr;
static VOID RecordWriteAddr(VOID * addr)
{
    WriteAddr = addr;
}

//L2 Data Cache Access - Write 
static VOID RecordMemWrite(VOID * ip)
{
    L1_D_CACHE->access((size_t)WriteAddr, (size_t)ip, true);
#ifdef _DEBUG_
    TraceFile << "@ " << dec << instCount << ", " << hex << ip << ": " << WriteAddr << " WRITE\n" ;
#endif
}

//L1 Instruction Cache Access 
VOID countFunc(VOID * ip)
{
  L1_I_CACHE->access((size_t)ip, (size_t)ip, false);
  instCount++;
}

VOID Instruction(INS ins, VOID *v)
{
    INS_InsertCall(ins,IPOINT_BEFORE,(AFUNPTR)countFunc, IARG_INST_PTR, IARG_END);

    // instruments loads using a predicated call, i.e.
    // the call happens iff the load will be actually executed
    if (INS_IsMemoryRead(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
            IARG_INST_PTR, IARG_MEMORYREAD_EA,
            IARG_END);
    }
    if (INS_HasMemoryRead2(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordMemRead,
            IARG_INST_PTR, IARG_MEMORYREAD2_EA,
            IARG_END);
    }
    // instruments stores using a predicated call, i.e.
    // the call happens iff the store will be actually executed
    if (INS_IsMemoryWrite(ins))
    {
        INS_InsertPredicatedCall(
            ins, IPOINT_BEFORE, (AFUNPTR)RecordWriteAddr,
            IARG_MEMORYWRITE_EA,
            IARG_END);
        
        if (INS_HasFallThrough(ins))
        {
            INS_InsertCall(
                ins, IPOINT_AFTER, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_END);
        }
        if (INS_IsBranchOrCall(ins))
        {
            INS_InsertCall(
                ins, IPOINT_TAKEN_BRANCH, (AFUNPTR)RecordMemWrite,
                IARG_INST_PTR,
                IARG_END);
        }
    }
}

/* ===================================================================== */

VOID Fini(INT32 code, VOID *v)
{
    TraceFile << "\ndbpSim: Cache Statistics : " << std::endl;
    TraceFile << "==============================================" << std::endl;
    TraceFile << std::dec;
    TraceFile << "L1 I_CACHE ACCESS COUNT: " << L1_I_CACHE->get_access_cnt() << std::endl;
    TraceFile << "L1 D_CACHE ACCESS COUNT: " << L1_D_CACHE->get_access_cnt() << std::endl;

    TraceFile << "L1 I_CACHE MISS COUNT: " << L1_I_CACHE->get_miss_cnt() << std::endl;
    TraceFile << "L1 D_CACHE MISS COUNT: " << L1_D_CACHE->get_miss_cnt() << std::endl;

    TraceFile << "L1 I_CACHE DEAD BLK PRED: " << L1_I_CACHE->get_dbp_cnt() << std::endl;
    TraceFile << "L1 D_CACHE DEAD BLK PRED: " << L1_D_CACHE->get_dbp_cnt() << std::endl;

    TraceFile << "L1 I_CACHE EVICTIONS: " << L1_I_CACHE->get_evicted_cnt() << std::endl;
    TraceFile << "L1 D_CACHE EVICTIONS: " << L1_D_CACHE->get_evicted_cnt() << std::endl;

    TraceFile << "L1 I_CACHE DBP MISS_PRED: " << L1_I_CACHE->get_dbp_miss_pred() << std::endl;
    TraceFile << "L1 D_CACHE DBP MISS_PRED: " << L1_D_CACHE->get_dbp_miss_pred() << std::endl;
 
    double l1i_accuracy = (double)(L1_I_CACHE->get_dbp_cnt() - L1_I_CACHE->get_dbp_miss_pred()) / (double) (L1_I_CACHE->get_dbp_cnt());
    double l1d_accuracy = (double)(L1_D_CACHE->get_dbp_cnt() - L1_D_CACHE->get_dbp_miss_pred()) / (double) (L1_D_CACHE->get_dbp_cnt());

    TraceFile << "L1 I_CACHE DBP ACCURACY : " << l1i_accuracy << std::endl;
    TraceFile << "L1 D_CACHE DBP ACCURACY : " << l1d_accuracy << std::endl;

    double l1i_cov = (double)(L1_I_CACHE->get_dbp_cnt() - L1_I_CACHE->get_dbp_miss_pred()) / (double) (L1_I_CACHE->get_evicted_cnt());
    double l1d_cov = (double)(L1_D_CACHE->get_dbp_cnt() - L1_D_CACHE->get_dbp_miss_pred()) / (double) (L1_D_CACHE->get_evicted_cnt());

    TraceFile << "L1 I_CACHE DBP COVERAGE : " << l1i_cov << std::endl;
    TraceFile << "L1 D_CACHE DBP COVERAGE : " << l1d_cov << std::endl;
    TraceFile << "L1 I_CACHE TCP Prefetches: " << L1_I_CACHE->get_tcp_pr_cnt() << std::endl;
    TraceFile << "L1 D_CACHE TCP Prefetches: " << L1_D_CACHE->get_tcp_pr_cnt() << std::endl;
    TraceFile << "L1 I_CACHE TCP Useless Prefetches: " << L1_I_CACHE->get_useless_pr_cnt() << std::endl;
    TraceFile << "L1 D_CACHE TCP Useless Prefetches: " << L1_D_CACHE->get_useless_pr_cnt() << std::endl;

    TraceFile << "L2 ACCESS COUNT: " << L2_CACHE->get_access_cnt() << std::endl;
    TraceFile << "L2 CACHE MISS COUNT: " << L2_CACHE->get_miss_cnt() << std::endl;
    TraceFile << "L2 CACHE DEAD BLK PRED: " << L2_CACHE->get_dbp_cnt() << std::endl;
    TraceFile << "L2 CACHE EVICTIONS: " << L2_CACHE->get_evicted_cnt() << std::endl;
    TraceFile << "L2 CACHE DBP MISS_PRED: " << L2_CACHE->get_dbp_miss_pred() << std::endl;
    double l2_accuracy = (double)(L2_CACHE->get_dbp_cnt() - L2_CACHE->get_dbp_miss_pred()) / (double) (L2_CACHE->get_dbp_cnt());
    double l2_cov = (double)(L2_CACHE->get_dbp_cnt() - L2_CACHE->get_dbp_miss_pred()) / (double) (L2_CACHE->get_evicted_cnt());
    TraceFile << "L2 CACHE DBP ACCURACY : " << l2_accuracy << std::endl;
    TraceFile << "L2 CACHE DBP COVERAGE : " << l2_cov << std::endl;

    TraceFile << "L2 CACHE TCP Prefetches: " << L2_CACHE->get_tcp_pr_cnt() << std::endl;
    TraceFile << "L2 CACHE TCP Useless Prefetches: " << L2_CACHE->get_useless_pr_cnt() << std::endl;
    TraceFile << "==============================================" << std::endl;

    delete L2_CACHE;
    delete L1_I_CACHE;
    delete L1_D_CACHE;

    TraceFile << "#eof" << endl;
    TraceFile.close();
}

/* ===================================================================== */
/* Main                                                                  */
/* ===================================================================== */

int main(int argc, char *argv[])
{
    string trace_header = string("#\n"
                                 "# Memory Access Trace Generated By Pin\n"
                                 "#\n");
    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }
    TraceFile.open(KnobOutputFile.Value().c_str());
    TraceFile.write(trace_header.c_str(),trace_header.size());
    TraceFile.setf(ios::showbase);

    std::cout << "\ndbpSim: Cache Configuration : " << std::endl;
    std::cout << "==============================================" << std::endl;
    std::cout << "L1 Cache Size (KB): " << L1_cache_total_kb.Value() << std::endl;
    std::cout << "L1 Block Size (B): " << L1_cache_block_b.Value() << std::endl;
    std::cout << "L1 Set Ways : " << L1_cache_assoc_w.Value() << std::endl;
    
    std::cout << "L2 Cache Size (KB): " << L2_cache_total_kb.Value() << std::endl;
    std::cout << "L2 Block Size (B): " << L2_cache_block_b.Value() << std::endl;
    std::cout << "L2 Set Ways : " << L2_cache_assoc_w.Value() << std::endl;
    std::cout << "==============================================\n" << std::endl;

    TcpEnabled = (tcp_enable.Value() == 0) ? false : true;
    L2_CACHE = new cacheSim(L2_cache_total_kb.Value(), L2_cache_block_b.Value(), L2_cache_assoc_w.Value(), 0); 
    L2_CACHE->dbp_use_refcount = true; 
    L1_I_CACHE = new cacheSim(L1_cache_total_kb.Value(), L1_cache_block_b.Value(), L1_cache_assoc_w.Value(), L2_CACHE); 
    L1_D_CACHE = new cacheSim(L1_cache_total_kb.Value(), L1_cache_block_b.Value(), L1_cache_assoc_w.Value(), L2_CACHE); 

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns
    PIN_StartProgram();
    
    RecordMemWrite(0);
    RecordWriteAddr(0);
    
    return 0;
}
/* ===================================================================== */
/* eof */
/* ===================================================================== */

