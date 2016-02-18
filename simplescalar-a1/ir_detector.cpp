#include <map>
#include <vector>
#include <iostream>
#include <algorithm>
#include <iterator>
#include <list>
#include <cassert>
#include "machine.h"
#include "regs.h"
#include "ir_counters.h"

/* IDENTIFYING INSTRUCTIONS */

//ONE way to read an integer register
#define READ_I_REG(N)			(regfile->regs_R[N])
#define READ_I_PREG(N)			(p_regifle->regs_R[N])

//TWO ways to read a floating point register
#define READ_F_REG(N)		(regfile->regs_F.f[(N)])
#define READ_F_PREG(N)		(p_regifle->regs_F.f[(N)])
#define READ_D_REG(N)		(regfile->regs_F.d[(N) >> 1])
#define READ_D_PREG(N)		(p_regifle->regs_F.d[(N) >> 1])

enum status_t
{
  ACTIVE=0, UNREF_WR=1, NON_MOD_WR=2, PRE_BRANCH=3, PROPAGATED=4
};

enum branch_t
{
  TAKEN=0, NOT_TAKEN=1, MIXED=2
};

//operand rename table entry; point to the latest producer
//an entry is "reset" on when a new value is written to reg/memory
struct ort_entry
{
  bool valid;
  bool referenced;
  int producer_idx;

  //CORNER case for UNREFERENCED writes: two registers are produced by a single instruction
  int ort_pair;

  //default constructor
  ort_entry() : valid(false), referenced(false), producer_idx(0), ort_pair(0) {} 
};


//dynamic instruction window FIFO entry
struct fifo_entry
{
  //keep track of producers
  unsigned src_idx1;
  unsigned src_idx2;
  unsigned src_idx3;

  //store FIFO indexes to consumers that are alive; 
  //if all of its dependent instructions are known and they have been selected for removal,
  //a predecessor instruction is also selected for removal; all dep. instrs are known
  //when another a write to the same reg/memory location occurs.
  std::list<unsigned> consumer_lst;

  fifo_entry() : src_idx1(0), src_idx2(0), src_idx3(0), consumer_lst() {}
};

//ORT tables; ld/st instructions write to ort_memory
std::map<md_addr_t, ort_entry> ort_memory; 
std::vector<ort_entry> ort_regfile(MD_TOTAL_REGS);

//conditional branch history buffer
std::map<md_addr_t, ort_entry> br_history; 

//the instruction window is implemented as a circular FIFO
//vector size is kept at the initial size (w_size) during simulation
std::vector<fifo_entry> instr_window;
unsigned head_idx;
unsigned tail_idx;
unsigned w_instr_cnt;

extern "C" void ir_detector_setup(size_t w_size)
{
  instr_window.resize(w_size);
  head_idx = 0;
  tail_idx = 0;
  w_instr_cnt = 0;
}

bool _nmod_check(bool is_float, regs_t * regfile, regs_t * p_regifle, int * r_in, int * r_out)
{
  if (is_float)
  {
     if (r_out[0] != DNA && (READ_F_REG(r_out[0]) == READ_F_REG(r_out[0]) && READ_D_REG(r_out[0]) == READ_D_REG(r_out[0]))) {
       if (r_out[1] != DNA) {
          if (READ_F_REG(r_out[1]) == READ_F_REG(r_out[1]) && READ_D_REG(r_out[1]) == READ_D_REG(r_out[1])){
            return true;
          }
       } else {
         return true;
       }
     }
  }
  else
  {
     //non-modifying write check on REG_0: if matched, instr selected for removal as it enters FIFO
     if (r_out[0] != DNA && (READ_I_REG(r_out[0]) == READ_I_PREG(r_out[0])))
     {
       if (r_out[1] != DNA) {
          if (READ_I_REG(r_out[1]) == READ_I_PREG(r_out[1])) {
            return true;
          }
       } else {
         return true;
       }
     }
  }
  return false;

}

extern "C" void process_new_instr(enum md_opcode op, struct regs_t * regfile, struct regs_t * p_regifle, int * r_in, int * r_out)
{
   //integer computation
   if (F_ICOMP & MD_OP_FLAGS(op))
   {
      if(_nmod_check(false, regfile, p_regifle, r_in, r_out))
      {
        sim_reg_nmod_wr++;
      }
   }
   //float computation
   else if (F_FCOMP & MD_OP_FLAGS(op))
   {
      if(_nmod_check(true, regfile, p_regifle, r_in, r_out))
      {
        sim_reg_nmod_wr++;
      }
   }
   //producer regfile ort, consumer for (memory & reg) ort 
   //LOAD instructions write to one register only 
   else if (F_LOAD & MD_OP_FLAGS(op))
   {
     //writing to a floating-point register [i.e. 32-63]
     if ( r_out[0] > 31 && r_out[0] < 64 ) {
        if (_nmod_check(true, regfile, p_regifle, r_in, r_out)) {
          sim_reg_nmod_wr++;
        }
     //writing to an integer register
     } else {
        if (_nmod_check(false, regfile, p_regifle, r_in, r_out)) {
          sim_reg_nmod_wr++;
        }
     }
   }
   //producer memory ort, consumer for reg ort
   else if (F_STORE & MD_OP_FLAGS(op))
   {

   }
   else if (F_COND & MD_OP_FLAGS(op))
   {

   }

   //CONSUMER update

   //Instruction FIFO update
}



//UPDATE CONSUMERS information in ORT table
void _update_consumer()
{

}


