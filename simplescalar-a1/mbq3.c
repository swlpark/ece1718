#include <stdio.h>

#define FIXED_WORD 0xCAFEBABE

int nm_mem_tgt[8];

/*
 * Microbenchmark 3 : count number of unpredictable branches
 */
int main(int argc, char *argv[])
{
  register int a, i;
  int size = atoi(argv[1]);

  register int const_store = FIXED_WORD;

  for(i=0; i<size; i=i+1)
  {
     //unpredictable branches = effectual branch
     if (i % 2)  {
       a =  const_store ^ 0x11111111;
     }
  }
}
