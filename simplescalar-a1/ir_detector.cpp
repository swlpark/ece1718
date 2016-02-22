/* 
* ir_detector.cpp:
* Implements the IR-detector which looks at the retired dynamic instructions and determine
* whehther a dynamic instruction is ineffectual. 
*
*/

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
#define READ_I_REG(N)		(regfile->regs_R[N])
#define READ_I_PREG(N)		(p_regifle->regs_R[N])

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
  size_t producer_idx;

  //CORNER case for UNREFERENCED writes: two registers are produced by a single instruction
  int ort_pair;

  //default constructor
  ort_entry() : valid(false), referenced(false), producer_idx(0), ort_pair(0) {} 
};

//dynamic instruction window FIFO entry
struct fifo_entry
{
  bool valid;

  //ORT output registers / memory locations; used for ORT invalidation upon window exit
  unsigned reg_out1;
  unsigned reg_out2;
  md_addr_t mem_out1;
  md_addr_t mem_out2;
  
  //keep track of producer entries inside the FIFO; used for follow transitively ineffectual instructions
  //-1 if it is not using the source operand 
  int src_idx1;
  int src_idx2;
  int src_idx3;
  //store number of this instruction's consumers that are "effectual"; if negative, the entry is already accounted for
  //transitive write
  int consumer_count;

  //if chk_ineff_br == true, this entry may be later determined to be ineffectual
  bool chk_ineff_br;
  md_addr_t branch_pc;

  fifo_entry() : valid(false), src_idx1(0), src_idx2(0), src_idx3(0), consumer_count(0), chk_ineff_br(false), branch_pc(0)
  {
    reg_out1 = 0;
    reg_out2 = 0;
    mem_out1 = 0;
    mem_out2 = 0;
  }
};

//branch target buffer; keep track of each branch's tgt address, flag whether if its target has changed
struct btb_entry
{
  md_addr_t tgt_addr;
  bool     const_tgt;
  unsigned valid_cnt;

  btb_entry() : tgt_addr(0), const_tgt(true), valid_cnt(0) {}
  btb_entry(md_addr_t n_pc) : tgt_addr(n_pc), const_tgt(true), valid_cnt(1) {}
};

//ORT tables; ld/st instructions write to ort_memory
std::unordered_map<md_addr_t, ort_entry> ort_memory; 
std::vector<ort_entry> ort_regfile(MD_TOTAL_REGS);

//conditional branch history buffer
std::unordered_map<md_addr_t, btb_entry> btb_map; 

//map to keep track of how many times instructions at a pc has been removed
std::map<md_addr_t, unsigned> removed_instr_map;

//the instruction window is implemented as a circular FIFO
//vector size is kept at the initial size (w_size) during simulation
std::vector<fifo_entry> instr_window;
size_t fifo_head;
size_t fifo_mid;
size_t w_instr_cnt;

//circular FIFO ptr increment routine
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

/*
 *  non-modifying register write checks
 */
bool _nmod_check(bool is_float, regs_t * regfile, regs_t * p_regifle, const int * r_out)
{
  if (is_float)
  {
     if (r_out[0] != DNA && (READ_F_REG(r_out[0]) == READ_F_REG(r_out[0]) && READ_D_REG(r_out[0]) == READ_D_REG(r_out[0]))) {
       //if instruction is writing to 2 registers, both of them have to be non-modifying
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
       //if instruction is writing to 2 registers, both of them have to be non-modifying
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
 *  non-modifying memory checks
 */
bool _nmod_check_mem()
{
  if (_mem_store_addr_0 != 0 && (_mem_store_new_word_0 == _mem_store_old_word_0))
  {
       //if instruction is writing to two addresses, both of them have to be non-modifying
     if (_mem_store_addr_1 != 0) {
        if (_mem_store_new_word_1 == _mem_store_old_word_1) {
          return true;
        }
     } else {
       return true;
     }
  }
  return false;
}

/*
 * Recursively remove instructions that are producing values for ineffectual instructions
 */
void _decrement_consumer(int p_indices[3])
{
   size_t i_idx; 
   for (int i =0; i < 3; i++) {
     if (p_indices[i] > 0) {
         i_idx = (size_t) p_indices[i];
         assert(instr_window[i_idx].valid);
         if (instr_window[i_idx].consumer_count > 1)
            instr_window[i_idx].consumer_count -= 1;
         else if (instr_window[i_idx].consumer_count == 1) {
            instr_window[i_idx].consumer_count = 0;

            //If this instruction is not current producer of any ORT table entry, recurse on its prodcuer
            //because no further instructions will become its consumer
            if (ort_regfile[instr_window[i_idx].reg_out1].producer_idx != i_idx
             && ort_regfile[instr_window[i_idx].reg_out2].producer_idx != i_idx
             && ort_memory[instr_window[i_idx].mem_out1].producer_idx != i_idx
             && ort_memory[instr_window[i_idx].mem_out1].producer_idx != i_idx
            ) {
               int producers[3] = {instr_window[i_idx].src_idx1,
                                   instr_window[i_idx].src_idx2,
                                   instr_window[i_idx].src_idx3};
               _decrement_consumer(producers);
            }
         }
     }
   }
}

/*
 * Src reference update; tag instructions that produce the source operands in the window
 */
void _ref_update(const int * r_in)
{
  for(int i=0; i<3; i++)
  {
    if(r_in[i] != DNA) {
      ort_regfile[r_in[i]].referenced = true;
      instr_window[ort_regfile[r_in[i]].producer_idx].consumer_count += 1;
    }
  }
  //check if the matching ORT entry exisits in the table and it is valid before tagging its reference
  if ((ort_memory.find(_mem_load_addr_0) != ort_memory.end()) && ort_memory[_mem_load_addr_0].valid  &&
      _mem_load_addr_0 != 0) {
     ort_memory[_mem_load_addr_0].referenced = true;
     instr_window[ort_memory[_mem_load_addr_0].producer_idx].consumer_count += 1;
  }
  if ((ort_memory.find(_mem_load_addr_1) != ort_memory.end()) && ort_memory[_mem_load_addr_1].valid  &&
      _mem_load_addr_1 != 0) {
     ort_memory[_mem_load_addr_1].referenced = true;
     instr_window[ort_memory[_mem_load_addr_1].producer_idx].consumer_count += 1;
  }
}

/*
* check references in ORT reg table, and check if the current producer is unused
*/
void _uref_check(const int * r_out)
{
  if (r_out[0] != DNA)
  {
    //check if the previous producer was ever referenced
    if (ort_regfile[r_out[0]].valid && !ort_regfile[r_out[0]].referenced && ort_regfile[r_out[0]].ort_pair == DNA ) {
      sim_reg_uref_wr++;

      //check if its source producers can be removed as well - transitively ineffectual instructions
      int producers[3] = {instr_window[ort_regfile[r_out[0]].producer_idx].src_idx1,
                          instr_window[ort_regfile[r_out[0]].producer_idx].src_idx2,
                          instr_window[ort_regfile[r_out[0]].producer_idx].src_idx3};
      _decrement_consumer(producers);
    }

    //update regfile ORT
    ort_regfile[r_out[0]].valid = true;
    ort_regfile[r_out[0]].referenced = false;
    ort_regfile[r_out[0]].producer_idx = fifo_head;
    ort_regfile[r_out[0]].ort_pair = r_out[1];
  }
  if (r_out[1] != DNA)
  {
    //check if the previous producer was ever referenced
    if (ort_regfile[r_out[1]].valid && !ort_regfile[r_out[1]].referenced && ort_regfile[r_out[1]].ort_pair == DNA ) {
      sim_reg_uref_wr++;

      //check if its source producers can be removed as well - transitively ineffectual instructions
      int producers[3] = {instr_window[ort_regfile[r_out[1]].producer_idx].src_idx1,
                          instr_window[ort_regfile[r_out[1]].producer_idx].src_idx2,
                          instr_window[ort_regfile[r_out[1]].producer_idx].src_idx3};
      _decrement_consumer(producers);

    }
    //update regfile ORT
    ort_regfile[r_out[1]].valid = true;
    ort_regfile[r_out[1]].referenced = false;
    ort_regfile[r_out[1]].producer_idx = fifo_head;
    ort_regfile[r_out[1]].ort_pair = r_out[0];
  }
}

/*
* check references in ORT memory table, and check if the current producer is unused
*/
void _uref_check_mem(const int * r_in)
{
  if (_mem_store_addr_0 != 0)
  {
    //check if the previous producer was ever referenced
    if ((ort_memory.find(_mem_store_addr_0) != ort_memory.end()) && ort_memory[_mem_store_addr_0].valid &&
        !ort_memory[_mem_store_addr_0].referenced && ort_memory[_mem_store_addr_0].ort_pair == DNA ) {
      sim_mem_uref_wr++;

      //check if its source producers can be removed as well - transitively ineffectual instructions
      int producers[3] = {instr_window[ort_memory[_mem_store_addr_0].producer_idx].src_idx1,
                          instr_window[ort_memory[_mem_store_addr_0].producer_idx].src_idx2,
                          instr_window[ort_memory[_mem_store_addr_0].producer_idx].src_idx3};
      _decrement_consumer(producers);
    }

    //update memory ORT
    ort_memory[_mem_store_addr_0].valid = true;
    ort_memory[_mem_store_addr_0].referenced = false;
    ort_memory[_mem_store_addr_0].producer_idx = fifo_head;
    ort_memory[_mem_store_addr_0].ort_pair = _mem_store_addr_1;
  }
  if (_mem_store_addr_1 != 0)
  {
    //check if the previous producer was ever referenced
    if ((ort_memory.find(_mem_store_addr_1) != ort_memory.end()) && ort_memory[_mem_store_addr_1].valid &&
        !ort_memory[_mem_store_addr_1].referenced && ort_memory[_mem_store_addr_1].ort_pair == DNA ) {
      sim_mem_uref_wr++;

      //check if its source producers can be removed as well - transitively ineffectual instructions
      int producers[3] = {instr_window[ort_memory[_mem_store_addr_1].producer_idx].src_idx1,
                          instr_window[ort_memory[_mem_store_addr_1].producer_idx].src_idx2,
                          instr_window[ort_memory[_mem_store_addr_1].producer_idx].src_idx3};
      _decrement_consumer(producers);
    }

    //update memory ORT
    ort_memory[_mem_store_addr_1].valid = true;
    ort_memory[_mem_store_addr_1].referenced = false;
    ort_memory[_mem_store_addr_1].producer_idx = fifo_head;
    ort_memory[_mem_store_addr_1].ort_pair = _mem_store_addr_0;
  }
}

//return the PC of the most removed instruction
extern "C" md_addr_t get_most_removed_instr()
{
  unsigned current_max = 0;
  md_addr_t key_max  = 0;
  for(auto it = removed_instr_map.cbegin(); it != removed_instr_map.cend(); ++it)
  {
     if(it->second > current_max) {
        key_max = it->first;
        current_max = it->second;
     }
  }
  return key_max;
}

/*
* Main IR-detector routine; update its ORT and instruction window data structure, and determine if
* dynamic instructions in the window or the incoming instruction is ineffectual or not
*/

extern "C" void process_new_instr(enum md_opcode op, struct regs_t * regfile, struct regs_t * p_regifle, const int * r_in, const int * r_out, md_addr_t pc, md_addr_t next_pc)
{
   //if the new instruction is "removed" on entry, the IR detector should not update producer/consumer in ORT/Instr Window 
   bool rm_on_entry = false;

   //new FIFO entry to be enqueued to the instructrion window
   fifo_entry incoming_instr;
   incoming_instr.valid = true;
   incoming_instr.src_idx1 = (r_in[0] != DNA) ? ort_regfile[r_in[0]].producer_idx : -1;
   incoming_instr.src_idx2 = (r_in[1] != DNA) ? ort_regfile[r_in[1]].producer_idx : -1;
   incoming_instr.src_idx3 = (r_in[2] != DNA) ? ort_regfile[r_in[2]].producer_idx : -1;

   incoming_instr.reg_out1 = r_out[0];
   incoming_instr.reg_out2 = r_out[1];
   incoming_instr.mem_out1 = _mem_store_addr_0;
   incoming_instr.mem_out2 = _mem_store_addr_1;

   //integer computation
   if (MD_OP_FLAGS(op) & F_ICOMP)
   {
      if ((rm_on_entry =_nmod_check(false, regfile, p_regifle, r_out)))
      {
        if (removed_instr_map.find(pc) == removed_instr_map.end())
        {
           removed_instr_map[pc] = 1;
        } else {
           removed_instr_map[pc] += 1;
        }
        sim_reg_nmod_wr++;
      }
   }
   //float computation
   else if (MD_OP_FLAGS(op) & F_FCOMP)
   {
      if ((rm_on_entry = _nmod_check(true, regfile, p_regifle, r_out)))
      {
        if (removed_instr_map.find(pc) == removed_instr_map.end())
        {
           removed_instr_map[pc] = 1;
        } else {
           removed_instr_map[pc] += 1;
        }
        sim_reg_nmod_wr++;
      }
   }
   //producer regfile ort, consumer for (memory & reg) ort 
   //LOAD instructions write to one register only 
   else if (MD_OP_FLAGS(op) & F_LOAD)
   {
     //writing to a floating-point register [i.e. 32-63]
     if ( r_out[0] > 31 && r_out[0] < 64 ) {
        if ((rm_on_entry = _nmod_check(true, regfile, p_regifle, r_out))) {
          if (removed_instr_map.find(pc) == removed_instr_map.end())
          {
             removed_instr_map[pc] = 1;
          } else {
             removed_instr_map[pc] += 1;
          }
          sim_reg_nmod_wr++;
        }
     //writing to an integer register
     } else {
        if ((rm_on_entry = _nmod_check(false, regfile, p_regifle, r_out))) {
          if (removed_instr_map.find(pc) == removed_instr_map.end())
          {
             removed_instr_map[pc] = 1;
          } else {
             removed_instr_map[pc] += 1;
          }
          sim_reg_nmod_wr++;
        }
     }
   }
   //producer memory ort, consumer for reg ort
   else if (MD_OP_FLAGS(op) & F_STORE)
   {
      if ((rm_on_entry = _nmod_check_mem())) {
        if (removed_instr_map.find(pc) == removed_instr_map.end())
        {
           removed_instr_map[pc] = 1;
        } else {
           removed_instr_map[pc] += 1;
        }
        sim_mem_nmod_wr++;
      }
   }
   //unconditional branches are marked as ineffectual upon entry
   else if (MD_OP_FLAGS(op) & F_CALL || MD_OP_FLAGS(op) & F_UNCOND)
   {
      if (removed_instr_map.find(pc) == removed_instr_map.end())
      {
         removed_instr_map[pc] = 1;
      } else {
         removed_instr_map[pc] += 1;
      }
      rm_on_entry = true;
      sim_inef_br++;
   }
   //conditional branches have to refer to branch-target-buffer later to determine
   //if they are ineffectual in the context of instruction window
   else if (MD_OP_FLAGS(op) & F_COND)
   {
      incoming_instr.branch_pc = pc;

      if (btb_map.find(pc) == btb_map.end() || btb_map[pc].valid_cnt == 0)
      {
        btb_map[pc] = btb_entry(next_pc);
      }
      else
      {
        if (btb_map[pc].tgt_addr != next_pc)
        {
           btb_map[pc].const_tgt = false;
        }
        else if (btb_map[pc].const_tgt)
        {
          //this branch may be ineffectual; keep track of it
          incoming_instr.chk_ineff_br = true;
        }

        //increment number of dynamic instances of this branch in the instruction window
        btb_map[pc].valid_cnt += 1;
      }
   }

   //if incoming instruction is not ineffectual entry,
   //update references and check for unreferenced writes in ORT table
   if (!rm_on_entry)
   {
      _ref_update(r_in);
      if (MD_OP_FLAGS(op) & F_STORE)
        _uref_check_mem(r_in);
      else
        _uref_check(r_out);
   }

   //Predictable branch checks
   //at the middle of instruction window, check if BTB indicates a target address has changed; if not
   //we consider that the branch instruction is likely to be an ineffectual instruction, and we trigger
   //decrement_consumer routine on its producers which may make them transitively ineffectual
   //not 100% accurate, but it is a reasonable compromise
   if (instr_window[fifo_mid].chk_ineff_br) {
      if (btb_map[instr_window[fifo_mid].branch_pc].const_tgt)
      {
        //check if its source producers can be removed as well - transitively ineffectual instructions
        int producers[3] = {instr_window[fifo_mid].src_idx1,
                            instr_window[fifo_mid].src_idx2,
                            instr_window[fifo_mid].src_idx3};
        _decrement_consumer(producers);

      }
   }
   //at the end of instruction window, check if BTB indicates a target address has changed; if not
   //the branch instruction is definitely an ineffectual branch
   if (instr_window[fifo_head].branch_pc != 0) {
      if (btb_map[instr_window[fifo_head].branch_pc].const_tgt)
      {
        sim_inef_br++;
        if (removed_instr_map.find(instr_window[fifo_head].branch_pc) == removed_instr_map.end())
        {
           removed_instr_map[instr_window[fifo_head].branch_pc] = 1;
        } else {
           removed_instr_map[instr_window[fifo_head].branch_pc] += 1;
        }
      }
      if (btb_map[instr_window[fifo_head].branch_pc].valid_cnt > 0)
          btb_map[instr_window[fifo_head].branch_pc].valid_cnt -= 1;
   }

   //Instruction Window size check; if the window is not yet full, no instruction gets evicted
   if (w_instr_cnt < instr_window.size()) {
     w_instr_cnt += 1;

   //Instruction is being evicted out from the window
   } else {
     //1) Invalidate ORT entry of the oldest instruction being evicted out from the window
     if(ort_regfile[instr_window[fifo_head].reg_out1].valid &&
        ort_regfile[instr_window[fifo_head].reg_out1].producer_idx == fifo_head)
     {
        ort_regfile[instr_window[fifo_head].reg_out1].valid = false;
     }
     if(ort_regfile[instr_window[fifo_head].reg_out2].valid &&
        ort_regfile[instr_window[fifo_head].reg_out2].producer_idx == fifo_head)
     {
        ort_regfile[instr_window[fifo_head].reg_out2].valid = false;
     }
     if((ort_memory.find(instr_window[fifo_head].mem_out1) != ort_memory.end()) && ort_memory[instr_window[fifo_head].mem_out1].valid
      && ort_memory[instr_window[fifo_head].mem_out1].producer_idx == fifo_head)
     {
        ort_memory[instr_window[fifo_head].mem_out1].valid = false;
     }
     if((ort_memory.find(instr_window[fifo_head].mem_out2) != ort_memory.end()) && ort_memory[instr_window[fifo_head].mem_out2].valid
      && ort_memory[instr_window[fifo_head].mem_out2].producer_idx == fifo_head)
     {
        ort_memory[instr_window[fifo_head].mem_out2].valid = false;
     }
     //End of 1)

     //2) If its consumer count is 0, it is deemed ineffectual ("no valid instruction consumes its value")
     if (instr_window[fifo_head].consumer_count == 0)
       sim_transitive_ineff++; 
   }

   //Instruction window update; increment FIFO indices
   instr_window[fifo_head] = incoming_instr;
   p_incr_fifo_ptr(fifo_head);
   p_incr_fifo_ptr(fifo_mid);
}
