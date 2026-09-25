// GPU program: C[i] = A[i] + B[i].
// Built with the bare-metal RISC-V compiler and run by every Vortex thread.
#include <vx_spawn.h>
#include "common.h"

// Runs once per element. blockIdx.x is this call's element number.
void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
  auto a = reinterpret_cast<float*>(arg->a_addr);
  auto b = reinterpret_cast<float*>(arg->b_addr);
  auto c = reinterpret_cast<float*>(arg->c_addr);

  uint32_t i = blockIdx.x;
  c[i] = a[i] + b[i];
}

int main() {
  // The driver put the note's address in register 0x68; the GPU exposes it in mscratch.
  auto arg = reinterpret_cast<kernel_arg_t*>(csr_read(VX_CSR_MSCRATCH));

  // Hand num_points tasks (one dimension) to all GPU threads.
  return vx_spawn_threads(1, &arg->num_points, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}
