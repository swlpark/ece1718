#include <map>
#include <unordered_map>
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
  unsigned consumer_count;

  fifo_entry() : src_idx1(0), src_idx2(0), src_idx3(0), consumer_count(0) {}
};

struct btb_entry
{
  //keep track of producers
  md_addr_t pc;
  md_addr_t next_pc;

  bool const_tgt;
  unsigned valid_cnt;

  btb_entry() : pc(0), next_pc(0), const_tgt(0), valid_cnt(0) {}
};


//ORT tables; ld/st instructions write to ort_memory
std::unordered_map<md_addr_t, ort_entry> ort_memory; 
std::vector<ort_entry> ort_regfile(MD_TOTAL_REGS);

//conditional branch history buffer
std::map<md_addr_t, btb_entry> btb; 

//the instruction window is implemented as a circular FIFO
//vector size is kept at the initial size (w_size) during simulation
std::vector<fifo_entry> instr_window;
size_t fifo_head;
size_t fifo_mid;
size_t w_instr_cnt;

size_t p_incr_fifo_ptr(size_t & idx)
{
   size_t retval = idx;
   if(idx < instr_window.size() - 1)
     idx += 1;
   else
     idx = 0;
   return retval; 
}

extern "C" void ir_detector_setup(size_t w_size)
{
  instr_window.resize(w_size);
  fifo_head = 0;
  fifo_mid = w_size / 2;
  w_instr_cnt = 0;
}

bool _nmod_check(bool is_float, regs_t * regfile, regs_t * p_regifle, const int * r_out)
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

/*
* Update consumer references into ORT table, and check if the value is unused
*/

void _uref_check(const int * r_in, const int * r_out)
{
  for(int i=0; i<3; i++)
  {
    if(r_in[i] != DNA) {
      ort_regfile[r_in[i]].referenced = true;
    }
  }

  if (r_out[0] != DNA)
  {
    //check if the previous producer was ever referenced
    if (ort_regfile[r_out[0]].valid && !ort_regfile[r_out[0]].referenced && ort_regfile[r_out[0]].ort_pair == DNA ) {
      sim_reg_uref_wr++;
    }

    //update regfile ORT
    ort_regfile[r_out[0]].valid = true;
    ort_regfile[r_out[0]].referenced = false;
    ort_regfile[r_out[0]].producer_idx = p_incr_fifo_ptr(fifo_head);
    ort_regfile[r_out[0]].ort_pair = r_out[1];
  }
  if (r_out[1] != DNA)
  {
    //check if the previous producer was ever referenced
    if (ort_regfile[r_out[1]].valid && !ort_regfile[r_out[1]].referenced && ort_regfile[r_out[1]].ort_pair == DNA ) {
      sim_reg_uref_wr++;
    }
    //update regfile ORT
    ort_regfile[r_out[1]].valid = true;
    ort_regfile[r_out[1]].referenced = false;
    ort_regfile[r_out[1]].producer_idx = p_incr_fifo_ptr(fifo_head);
    ort_regfile[r_out[1]].ort_pair = r_out[0];
  }
}

extern "C" void process_new_instr(enum md_opcode op, struct regs_t * regfile, struct regs_t * p_regifle, const int * r_in, const int * r_out, md_addr_t pc, md_addr_t next_pc)
{
   bool rm_on_entry = false;

   //integer computation
   if (F_ICOMP & MD_OP_FLAGS(op))
   {
      if ((rm_on_entry =_nmod_check(false, regfile, p_regifle, r_out)))
      {
        sim_reg_nmod_wr++;
      }
   }
   //float computation
   else if (F_FCOMP & MD_OP_FLAGS(op))
   {
      if ((rm_on_entry = _nmod_check(true, regfile, p_regifle, r_out)))
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
        if ((rm_on_entry = _nmod_check(true, regfile, p_regifle, r_out))) {
          sim_reg_nmod_wr++;
        }
     //writing to an integer register
     } else {
        if ((rm_on_entry = _nmod_check(false, regfile, p_regifle, r_out))) {
          sim_reg_nmod_wr++;
        }
     }
   }
   //producer memory ort, consumer for reg ort
   else if (F_STORE & MD_OP_FLAGS(op))
   {

   }
   //unconditional branches
   else if (MD_OP_FLAGS(op) & F_CALL || MD_OP_FLAGS(op) & F_UNCOND)
   {
      //rm_on_entry = true;
      sim_inef_br++;
   }
   else if (MD_OP_FLAGS(op) & F_COND)
   {
      //rm_on_entry = true;
      sim_inef_br++;
   }

   if (!rm_on_entry)
   {
      _uref_check(r_in, r_out);
   }

   //Instruction FIFO update
   
}
