typedef unsigned int UINT32;

struct ort_entry {
  UINT32 producer_idx;
  INT32 value;
  char valid;
  char referenced;
};

