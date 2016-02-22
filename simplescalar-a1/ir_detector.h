#ifndef IR_DETECTOR_H
#define IR_DETECTOR_H

#include "host.h"
#include "misc.h"
#include "machine.h"
#include "regs.h"
 
enum status_t
{
  ACTIVE=0, UNREF_WR=1, NON_MOD_WR=2, PRE_BRANCH=3, PROPAGATED=4
};

enum branch_t
{
  TAKEN=0, NOT_TAKEN=1, MIXED=2
};


void ir_detector_setup(size_t w_size);
md_addr_t get_most_removed_instr();
void process_new_instr(enum md_opcode op, struct regs_t * regfile, struct regs_t * p_regifle, const int * r_in, const int * r_out, md_addr_t pc, md_addr_t next_pc);

#endif
