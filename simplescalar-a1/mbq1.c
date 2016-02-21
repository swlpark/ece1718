#include <stdio.h>

#define FIXED_WORD 0xCAFEBABE

/*
* Microbenchmark 1 : check non-modifying register writes to nm_reg_tgt
*/
int main(int argc, char *argv[])
{
  int i;
  int size = atoi(argv[1]);

  register int const_store = FIXED_WORD;
  register int nm_reg_tgt = 0;

  for(i=0; i<size; i=i+1)
  {
     nm_reg_tgt = const_store ^ 0x11111111;
  }

}
