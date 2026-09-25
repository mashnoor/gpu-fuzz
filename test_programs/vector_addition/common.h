// The "note" the CPU program leaves in GPU memory for the GPU program.
// Both sides include this file, so they agree on the layout byte for byte.
#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

typedef struct {
  uint32_t num_points;   // how many elements
  uint64_t a_addr;       // GPU address of input  A
  uint64_t b_addr;       // GPU address of input  B
  uint64_t c_addr;       // GPU address of output C
} kernel_arg_t;

#endif
