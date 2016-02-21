#include <stdio.h>

#define FIXED_WORD 0xCAFEBABE

int nm_mem_tgt[8];

/*
* Microbenchmark 2 : check non-modifying memory writes to nm_reg_tgt
*                    check transitive 
*/
int main(int argc, char *argv[])
{
  int i;
  int size = atoi(argv[1]);

  register int const_store = FIXED_WORD;

  for(i=0; i<size; i=i+1)
  {
     int register a_idx = i & 7;
     nm_mem_tgt[a_idx] = const_store ^ 0x11111111;
  }
}
