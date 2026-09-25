// CPU program: computes C = A * B on the Vortex GPU and checks the result.
//
// Runs inside the RISC-V Linux guest. Every vx_* call goes to the UMD
// (libvortex.so -> libvortex-esp.so); only vx_start crosses into the kernel
// driver (KMD), which launches the GPU and sleeps until its interrupt.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define OP_NAME   "vector_multiplication"
#define OP_SYMBOL "*"
static float reference(float a, float b) { return a * b; }   // what the GPU must match

static const char* kernel_file = "/applications/test/vortex_kernels/vector_multiplication.vxbin";
static uint32_t num_points = 16;

static vx_device_h device = nullptr;
static vx_buffer_h a_buf = nullptr, b_buf = nullptr, c_buf = nullptr;
static vx_buffer_h krnl_buf = nullptr, args_buf = nullptr;

static void cleanup() {
  if (!device)
    return;
  if (a_buf)    vx_mem_free(a_buf);
  if (b_buf)    vx_mem_free(b_buf);
  if (c_buf)    vx_mem_free(c_buf);
  if (krnl_buf) vx_mem_free(krnl_buf);
  if (args_buf) vx_mem_free(args_buf);
  vx_dev_close(device);   // also prints the GPU's "PERF:" counters
  device = nullptr;
}

#define CHECK(expr)                                              \
  do {                                                           \
    int rc_ = (expr);                                            \
    if (rc_ != 0) {                                              \
      printf("Error: '%s' returned %d\n", #expr, rc_);           \
      cleanup();                                                 \
      exit(1);                                                   \
    }                                                            \
  } while (0)

// Distance between two floats in "units in the last place": 0 means bit-identical.
static int64_t ordered_bits(float f) {
  int32_t i;
  memcpy(&i, &f, sizeof(i));
  return i < 0 ? -(int64_t)(i & 0x7fffffff) : (int64_t)i;
}
static uint64_t ulp_distance(float x, float y) {
  int64_t d = ordered_bits(x) - ordered_bits(y);
  return d < 0 ? -d : d;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "n:k:h")) != -1) {
    switch (c) {
    case 'n': num_points = atoi(optarg); break;
    case 'k': kernel_file = optarg; break;
    default:
      printf("Usage: %s [-n elements] [-k kernel.vxbin]\n", argv[0]);
      exit(c == 'h' ? 0 : 1);
    }
  }
  if (num_points == 0) {
    printf("Error: -n must be at least 1\n");
    exit(1);
  }
}

int main(int argc, char** argv) {
  parse_args(argc, argv);
  const uint64_t bytes = uint64_t(num_points) * sizeof(float);
  printf("%s: C = A %s B, n=%u, kernel=%s\n", OP_NAME, OP_SYMBOL, num_points, kernel_file);

  // 1. Open the GPU (loads libvortex-esp.so, opens /dev/gt_vortex_rtl.0 and /dev/mem).
  CHECK(vx_dev_open(&device));

  // 2. Reserve GPU memory and write each buffer's GPU address into the note.
  kernel_arg_t arg = {};
  arg.num_points = num_points;
  CHECK(vx_mem_alloc(device, bytes, VX_MEM_READ, &a_buf));
  CHECK(vx_mem_address(a_buf, &arg.a_addr));
  CHECK(vx_mem_alloc(device, bytes, VX_MEM_READ, &b_buf));
  CHECK(vx_mem_address(b_buf, &arg.b_addr));
  CHECK(vx_mem_alloc(device, bytes, VX_MEM_WRITE, &c_buf));
  CHECK(vx_mem_address(c_buf, &arg.c_addr));
  printf("GPU addresses: A=0x%lx B=0x%lx C=0x%lx\n",
         (unsigned long)arg.a_addr, (unsigned long)arg.b_addr, (unsigned long)arg.c_addr);

  // 3. Make the inputs on the CPU, in [-10, 10).
  std::vector<float> a(num_points), b(num_points), c(num_points);
  srand(42);
  for (uint32_t i = 0; i < num_points; ++i) {
    a[i] = float(rand()) / float(RAND_MAX) * 20.0f - 10.0f;
    b[i] = float(rand()) / float(RAND_MAX) * 20.0f - 10.0f;
  }

  // 4. Copy inputs to the GPU. Fill C with a garbage pattern first, so a GPU
  //    that writes nothing fails the check instead of passing on zeroed memory.
  CHECK(vx_copy_to_dev(a_buf, a.data(), 0, bytes));
  CHECK(vx_copy_to_dev(b_buf, b.data(), 0, bytes));
  std::vector<uint32_t> garbage(num_points, 0xDEADBEEF);
  CHECK(vx_copy_to_dev(c_buf, garbage.data(), 0, bytes));

  // 5. Upload the GPU program and the note.
  CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buf));
  CHECK(vx_upload_bytes(device, &arg, sizeof(arg), &args_buf));

  // 6. Launch. This is the one call that reaches the kernel driver; it
  //    returns after the GPU raises its completion interrupt.
  printf("launching GPU...\n");
  CHECK(vx_start(device, krnl_buf, args_buf));
  CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  // 7. Copy the result back and compare with the CPU's own answer.
  CHECK(vx_copy_from_dev(c.data(), c_buf, 0, bytes));

  uint32_t errors = 0;
  uint64_t worst_ulp = 0;
  for (uint32_t i = 0; i < num_points; ++i) {
    float expected = reference(a[i], b[i]);
    uint64_t ulp = ulp_distance(c[i], expected);
    if (ulp > worst_ulp)
      worst_ulp = ulp;
    if (i < 4)
      printf("  [%u] %f %s %f = %f (expected %f)\n", i, a[i], OP_SYMBOL, b[i], c[i], expected);
    if (ulp > 0) {
      if (errors < 10)
        printf("  *** mismatch at [%u]: got %f, expected %f (%lu ulp)\n",
               i, c[i], expected, (unsigned long)ulp);
      ++errors;
    }
  }

  cleanup();

  if (errors) {
    printf("%s FAILED: %u of %u elements wrong\n", OP_NAME, errors, num_points);
    return 1;
  }
  printf("%s PASSED: all %u elements bit-exact\n", OP_NAME, num_points);
  return 0;
}
