#include <map>
#include <vector>
#include <iostream>
#include <algorithm>
#include <iterator>
#include <list>
#include "machine.h"
#include "regs.h"

//operand rename table entry; store producers
//an entry is "reset" on when a new value is written to reg/memory
struct ort_entry
{
  bool valid;
  bool referenced;

  //store FIFO indexes to consumers that are alive; 
  //if all of its dependent instructions are known and they have been selected for removal,
  //a predecessor instruction is also selected for removal; all dep. instrs are known
  //when another a write to the same reg/memory location occurs.
  std::list<unsigned> consumer_idx;
  
  unsigned producer_idx;

  //default constructor
  ort_entry() : valid(false), referenced(false), consumer_idx(), producer_idx(0) {} 
};

enum status_t
{
  ACTIVE=0, UNREF_WR=1, NON_MOD_WR=2, PRE_BRANCH=3, PROPAGATED=4
};

enum branch_t
{
  TAKEN=0, NOT_TAKEN=1, MIXED=2
};

//dynamic instruction window FIFO entry
struct fifo_entry
{
  //store opcode of instr
  md_addr_t instr_addr;

  //record the reason for instr removal, if deemed ineffectual
  status_t status;
  fifo_entry() : instr_addr(0), status(ACTIVE) {}
};

//ORT tables; ld/st instructions write to ort_memory
std::map<md_addr_t, ort_entry> ort_memory; 
std::vector<ort_entry> ort_regfile(MD_TOTAL_REGS);

//conditional branch history buffer
std::map<md_addr_t, ort_entry> br_history; 

//the instruction window is implemented as a circular FIFO
std::vector<fifo_entry> instr_window;
std::vector<fifo_entry>::iterator head_idx;
std::vector<fifo_entry>::iterator tail_idx;
