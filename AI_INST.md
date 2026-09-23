# ESP/Vortex Linux-to-RTL continuation handoff

Last updated: 2026-09-10 on `lab-computer` (Ubuntu/Linux x86-64).

**STATUS: the acceptance test passes.** The full path -- RISC-V Linux
application -> ESP UMD -> ESP Linux KMD -> ESP MMIO registers -> real
GT_VORTEX_wrapper/Vortex RTL under Verilator -> guest physical RAM over AXI ->
completion interrupt back to the KMD -> application verifies output -- runs end
to end with no mock anywhere in it. Reproduce with `guest/run-guest.sh --auto`.
See "Acceptance test result" below for the evidence.

## User objective

Establish and verify this real full-stack path without requiring an FPGA:

```text
RISC-V Linux application
  -> Vortex user-mode driver (libvortex.so + libvortex-esp.so)
  -> ESP/Vortex Linux kernel-mode driver
  -> ESP-style MMIO registers
  -> actual GT_VORTEX_wrapper/Vortex RTL under Verilator
  -> guest physical RAM through AXI
  -> completion interrupt back to the Linux KMD
  -> application verifies the output
```

Do not call a standalone `VORTEX_DRIVER=rtlsim` test the final result. It uses
actual Vortex RTL, but bypasses the Linux KMD and ESP wrapper path.

Read `GPU_FUZZING_HANDOFF.md` before continuing. It contains the original
research goal, source map, pitfalls, and acceptance criterion.

## Repository locations and pinned revisions

Workspace:

```text
/home/nowfel/lab-works/fuzzing/gpu_fuzz
```

Relevant trees:

```text
esp/                   ESP integration source (pinned; not modified)
qemu-v8.2.2/           QEMU source with the esp-vortex device model
qemu-vortex-bridge/    command ledger, RTL bridge library, and host self-test
guest/                 RISC-V Linux guest: kernel, ESP modules, rootfs, run script
```

Pinned revisions currently checked out:

```text
ESP:     ed7ed2e3daba852c2bc89c9b74c87053c53181ec
Vortex:  ecf734d632644dc3726c2bfab76d463ff505ffa5
QEMU:    11aa0b1ff115b86160c4d37e7c37e6a6b13b77ea (tag v8.2.2)
```

Keep the ESP fork of Vortex at its pinned revision. Do not replace it with the
separate top-level `vortex/` repository.

## Completed and verified work

### Host tools

Available tools include:

```text
qemu-system-riscv64 8.2.2
riscv64-linux-gnu-gcc
Verilator 5.034 from /home/nowfel/tools/verilator/bin/verilator
GHDL
Vortex LLVM and RISC-V tools under /home/nowfel/tools
```

The following QEMU build dependencies were installed through apt:

```text
libglib2.0-dev
libpixman-1-dev
zlib1g-dev
ninja-build
```

Do not record or request the user's sudo password in repository files.

### Pinned Vortex dependencies

The following nested submodules were initialized:

```text
third_party/fpnew      7224de444b2bf8bf2b8fe06bddbce0acd50773ad
third_party/ramulator  e62c84a6f0e06566ba6e182d308434b4532068a5
third_party/softfloat  b51ef8f3201669b2288104c28546fc72532a1ea4
```

Vortex was configured with:

```sh
./configure --xlen=64 --tooldir=/home/nowfel/tools
```

The initial runtime build failed because `hw/VX_config.h` and `hw/VX_types.h`
had not been generated. The required ordering is:

```sh
make -C hw
make -C third_party softfloat
make -C third_party ramulator
make -C runtime/rtlsim -j4
```

These artifacts now exist:

```text
runtime/librtlsim.so
runtime/libvortex.so
runtime/libvortex-rtlsim.so
kernel/libvortex.a
tests/regression/basic/basic
tests/regression/basic/kernel.vxbin
```

The correct path is `runtime/librtlsim.so`, not
`sim/rtlsim/librtlsim.so`.

### Standalone actual-RTL test passed

The first `basic` RTL run reached execution but failed with:

```text
VX_fetch.sv:95: invalid PC=0x0
```

This pinned integration has conflicting defaults: the regression Makefile
links the GPU program at address zero, while the RTL has an assertion rejecting
a zero fetch PC. The minimal working correction was to link the GPU kernel at
`0x1000`:

```sh
make -C tests/regression/basic clean-kernel
make -C tests/regression/basic STARTUP_ADDR=0x1000 all -j4
make -C tests/regression/basic STARTUP_ADDR=0x1000 run-rtlsim OPTS="-n1"
```

The user confirmed that this passed. The current ELF entry point is `0x1000`.
This verifies:

```text
x86 host test -> libvortex.so -> libvortex-rtlsim.so
-> Verilated Vortex RTL -> simulated memory -> correct output
```

It does not yet verify the ESP UMD, KMD, wrapper, QEMU guest, or interrupt.

## Command ledger requested by the user

The executable command file is:

```text
qemu-vortex-bridge/COMMANDS.sh
```

Keep updating that file with commands in execution order. Do not append final
acceptance commands until their prerequisites really exist. It currently
contains the verified Vortex build/test and QEMU configure/build commands.

## QEMU bridge work: built and verified

QEMU v8.2.2 was cloned into `qemu-v8.2.2/`. Its `build-riscv64/` directory was
configured with:

```sh
../configure --target-list=riscv64-softmmu --disable-werror --enable-debug
```

It has since been compiled (`ninja qemu-system-riscv64`) and the binary reports
`QEMU emulator version 8.2.2 (v8.2.2-dirty)`. The device model, the RTL bridge,
and their guest-visible behavior are now verified — see "Verified bridge
results" below.

### QEMU source changes

The following uncommitted changes were made inside `qemu-v8.2.2/`:

```text
hw/misc/esp_vortex.c       new device model
hw/misc/meson.build        compiles the new device
include/hw/riscv/virt.h    address and IRQ constants
hw/riscv/virt.c            creates device and device-tree node
```

The chosen guest-visible topology is:

```text
MMIO base: 0x10010000
MMIO size: 0x200 bytes
PLIC IRQ:  12
DT compatible: "GATech,gt_vortex"
MMIO endianness: DEVICE_BIG_ENDIAN
```

The big-endian MMIO setting is intentional because the existing KMD uses
`iowrite32be()` and `ioread32be()`.

The device implements storage for the common ESP register bank and returns
1024 from `PT_NCHUNK_MAX_REG` at offset `0x18`. Vortex-specific offsets from
`0x50` upward are delegated to a dynamically loaded RTL bridge. Writing one to
the common ESP `CMD_REG` at offset `0x00` starts a worker thread. When the RTL
bridge returns, QEMU raises PLIC IRQ 12. Writing zero to `CMD_REG` lowers it.

The QEMU device loads the bridge shared library from:

```text
ESP_VORTEX_BRIDGE_LIB=/absolute/path/to/libesp_vortex_bridge.so
```

Without that environment variable, QEMU allows MMIO probing but refuses to run
a launch. This mode is only for bring-up and must not be treated as acceptance.

### Dynamic bridge ABI expected by QEMU

The shared library exports these exact C symbols (implemented in
`qemu-vortex-bridge/esp_vortex_bridge.{h,cpp}`):

```c
void *esp_vortex_bridge_create(read_callback, write_callback, void *opaque);
void esp_vortex_bridge_destroy(void *bridge);
void esp_vortex_bridge_reset(void *bridge);
uint32_t esp_vortex_bridge_read(void *bridge, uint32_t offset);
void esp_vortex_bridge_write(void *bridge, uint32_t offset, uint32_t value);
int esp_vortex_bridge_run(void *bridge);
```

The memory callbacks have these shapes:

```c
int read_callback(void *opaque, uint64_t guest_physical_address,
                  void *data, size_t length);
int write_callback(void *opaque, uint64_t guest_physical_address,
                   const void *data, size_t length);
```

The new QEMU device uses these callbacks to access actual QEMU guest physical
RAM through `dma_memory_read()` and `dma_memory_write()`.

## Verified bridge results

Steps 1 through 8 of the previous plan are done. Everything below was observed
on `lab-computer`, not inferred. Reproduce with `qemu-vortex-bridge/COMMANDS.sh`.

### 1-3. Patched QEMU builds and instantiates the device

QEMU compiled with no source changes needed beyond what was already written.

**Correction to the previous plan:** `-device help | grep esp-vortex` prints
nothing, and that is expected, not a failure. `esp-vortex` is a sysbus device
created by the `virt` machine itself; sysbus devices are not user-creatable and
never appear in `-device help`. Query QOM instead:

```sh
printf '{"execute":"qmp_capabilities"}\n{"execute":"qom-list-types"}\n{"execute":"quit"}\n' \
  | qemu-system-riscv64 -M virt -m 256M -display none -serial none -qmp stdio -S 2>/dev/null \
  | tr '{' '\n' | grep -i vortex
```

That prints `"name": "esp-vortex", "parent": "sys-bus-device"`. Note that
`-qtest`/`-qmp stdio` conflicts with `-nographic`; use `-display none -serial
none` instead.

The `virt` DTB contains the expected node (verified with `fdtdump`):

```text
gt-vortex@10010000 { interrupts = <0x0000000c>; interrupt-parent = <0x00000003>;
                     reg = <0x0 0x10010000 0x0 0x200>;
                     compatible = "GATech,gt_vortex"; };
```

### 4-8. `libesp_vortex_bridge.so` runs the real RTL

Sources live in `qemu-vortex-bridge/`:

```text
esp_vortex_bridge.h     the ABI shared with hw/misc/esp_vortex.c
esp_vortex_bridge.cpp   Verilated GT_VORTEX_wrapper + APB driver + AXI slave
Makefile                `make` builds the library, `make lint` lints the RTL
selftest/               host-side stand-in for QEMU (see below)
```

Verilator here is **5.034** (`/home/nowfel/tools/verilator/bin/verilator`).

**Required configuration discovery:** the wrapper must be built with
`-DMEM_ADDR_WIDTH=32`. `VX_config.vh` defaults it to 48 under `XLEN_64`, but the
ESP socket gives `GT_VORTEX_wrapper` a 32-bit AXI address bus, so the default
trips `Vortex_axi`'s `STATIC_ASSERT(AXI_ADDR_WIDTH >= MEM_ADDR_WIDTH)` under
`SIMULATION` and silently truncates addresses at the wrapper port under
synthesis. With `MEM_ADDR_WIDTH=32` the wrapper lints clean (0 errors) and
elaborates. All ESP-map raw addresses stay below 256 MiB, so 32 bits is ample.

Design points worth keeping:

- APB writes from QEMU are **queued**, not applied immediately. The RTL clock is
  owned by the QEMU worker thread, so `esp_vortex_bridge_run()` replays the
  queued writes as real APB transactions (one posedge each with
  `psel & penable & pwrite`) before running. This preserves the KMD's ordering,
  including its `0x54` start pulse, which the wrapper self-clears after one
  cycle.
- Register read-back returns the last written value for the holding registers.
  `0x54` (start) and `0x58` (busy) are hardware-owned and read from the RTL.
- The AXI slave mirrors the proven `sim/rtlsim/processor.cpp` model: single-beat
  bursts, `rlast=1`, tag-carried IDs, byte-strobe-honouring writes, and
  `awready/wready/arready` held low unless a launch is in progress. That gating
  matters: the wrapper leaves `Vortex_axi` **out of reset** until the first soft
  reset, so the core free-runs with randomized state before the first launch and
  must not be allowed to touch guest RAM. Verilator assertions are likewise
  disabled outside a launch and enabled during one.
- A consequence of that same wrapper behavior: **`0x58` reads 1 before the first
  launch**. That is the real RTL's state, not a bug in the bridge, and it is
  harmless because the ESP IRQ handler only reads `0x58` after a completion
  interrupt. Do not paper over it with a fake value.
- Vortex kernel `printf` output (raw address `IO_COUT_ADDR` = `0x0FF00000`) is
  intercepted and printed instead of being written to guest RAM. The bridge
  subtracts the `0x50` base register to recover the raw address.
- Watchdog: `ESP_VORTEX_BRIDGE_TIMEOUT` cycles, default 200e6. Expiry returns
  `-2` and QEMU still raises the interrupt, so a hung launch surfaces as a
  failed run rather than a hung guest.

### Verified: real RTL executes a real kernel (host, no Linux)

`qemu-vortex-bridge/selftest/` builds `libvortex-bridge.so`, a Vortex runtime
backend that plays QEMU's two roles: it owns the guest RAM window and issues the
same register sequence as `gt_vortex_prep_xfer()`. The stock regression binary
runs against it unmodified:

```sh
VORTEX_DRIVER=bridge VORTEX_BRIDGE_LIB=.../libesp_vortex_bridge.so ./basic -t 1 -n 1
```

Results: `Test PASSED`, exit status 0, for `-n 1`, `-n 8`, `-n 16` and `-n 64`.
`-n 1` completes in 3072 RTL cycles with 49 AXI reads and 32 AXI writes.

This is an RTL/bridge smoke test. It is **not** the acceptance test: no Linux
kernel, no ESP KMD, no ioctl, no interrupt delivery.

### Verified: the same launch through QEMU, with DMA and PLIC interrupt

`selftest/qtest_launch.py` replays a captured launch through the QEMU device
using qtest, so it needs no guest code. Run the regression test with
`VORTEX_BRIDGE_DUMP=<file>` to capture the pre-launch memory image, the launch
register values, and the reference results, then:

```sh
ESP_VORTEX_BRIDGE_LIB=.../libesp_vortex_bridge.so \
  python3 selftest/qtest_launch.py /tmp/vortex-launch.txt --qemu .../qemu-system-riscv64
```

Observed, exit status 0:

- `PT_NCHUNK_MAX` at `0x18` reads 1024 through `ioread32be` semantics, so the
  ESP common driver's `esp_xfer_input_ok()` check will pass.
- `BASE_ADDR` written as the KMD's `iowrite32be(0xa5000000)` arrives at the RTL
  as `0xa5000000` and reads back correctly. The `DEVICE_BIG_ENDIAN` region plus
  `iowrite32be` combination is therefore correct, not double-swapped.
- `CMD_REG=1` starts the RTL; it completes in 3655 cycles having done 52 AXI
  reads and 39 AXI writes **against QEMU guest RAM**.
- PLIC IRQ 12 becomes pending (`readl 0xc001000` bit 12) about 0.1 s later.
- `0x58` reads 0 at interrupt time, which is what the ESP third-party IRQ
  handler requires to declare `done`.
- Writing `CMD_REG=0`, as that handler does, clears the pending bit.
- The result buffer the RTL wrote into guest RAM matches the host reference run
  byte for byte, as does the MPM performance-counter region.

## Acceptance test result

Run it with:

```sh
guest/run-guest.sh --auto          # boot, test, power off
guest/run-guest.sh --auto -n 64    # larger workload
guest/run-guest.sh                 # interactive shell in the guest (Ctrl-A X to quit)
```

Observed inside the RISC-V guest, at `-n 1`, `-n 16` and `-n 64`:

```text
=== PLIC interrupt counters BEFORE the launch ===
 14:          0  SiFive PLIC  12 Edge      esp
...
Test PASSED
exit status: 0
=== PLIC interrupt counters AFTER the launch ===
 14:          1  SiFive PLIC  12 Edge      esp
```

Every acceptance criterion is met:

- The application prints `Test PASSED` and returns zero.
- The launch really went through the ioctl path: the modules load and bind
  (`gt_vortex_rtl 10010000.gt-vortex: device registered.`), `/dev/gt_vortex_rtl.0`,
  `/dev/contig_alloc` and `/dev/mem` all exist, and the app runs with
  `VORTEX_DRIVER=esp` against `libvortex-esp.so`.
- The completion interrupt really reached Linux: the PLIC counter for the `esp`
  handler on IRQ 12 increments from 0 to 1 across the launch.
- The RTL really executed: with `--verbose`, the bridge logs the KMD's own APB
  writes (`0x50 <= 0xa5000000`, `0x60 <= 0x00001000`, `0x68 <= 0x0ff10080`, the
  `0x54` start pulse) and reports `launch complete after 3072 cycles
  (reads=49, writes=32)` against QEMU guest RAM.
- No mock completion path exists: `esp_vortex_bridge_run()` returns success only
  when the RTL itself pulses `busy_interrupt`.
- `PERF: instrs=27, cycles=2397` at `-n 1` and `instrs=531, cycles=7684` at
  `-n 64` match the host-side reference runs exactly, cycle for cycle.

## Guest build (`guest/`)

```text
guest/linux-6.6.156/      guest kernel source + build
guest/esp-modules/        the four ESP kernel modules, ported to 6.6
guest/esp-modules/esp-6.6-port.patch   exact delta from the pinned ESP sources
guest/esp-soc/socgen/esp/soc_defs.h    hand-written stand-in for ESP's generated header
guest/rootfs/             initramfs contents (busybox, libs, modules, test)
guest/initramfs.cpio.gz   packed initramfs
guest/run-guest.sh        boot + acceptance-test driver
```

**Kernel choice: 6.6.156 LTS, deliberately not ESP's pinned 5.1.0.** 5.1 does
not build with the GCC 13.3 cross-compiler present here, and RISC-V 5.1 does not
select `ARCH_HAS_KCOV`, which the driver-side coverage the fuzzing goal needs
depends on. The pinned kernel buys nothing here because the DUT is the
accelerator, not the kernel.

**The port to 6.6 is two lines.** `class_create()` lost its owner argument in
Linux 6.4; `contig_alloc.c` and `esp.c` each needed one call site updated. That
is the entire delta -- see `guest/esp-modules/esp-6.6-port.patch`. Regenerate it
with `make -C guest/esp-modules patch`. The pinned ESP tree itself is untouched.

**`soc_defs.h` was hand-written.** Every ESP driver includes it, and ESP normally
generates it from an `esp-xconfig` SoC description via
`tools/socgen/socmap_gen.py:print_soc_defines()`. It is only eight defines, so
`guest/esp-soc/socgen/esp/soc_defs.h` describes a 1x1 "SoC" with one CPU, one
memory and one accelerator. No `esp-xconfig` run is needed.

**Guest memory plan.** QEMU `virt` RAM starts at `0x80000000`; the guest boots
with `-m 2G` and `mem=512M`:

```text
0x80000000 - 0xA0000000   Linux RAM (mem=512M keeps Linux inside this)
0xA0000000 - 0xA1000000   contig_alloc pool (module params start=/size=)
0xA5000000 - 0xB5000000   Vortex window, reached by the UMD through /dev/mem
```

The RTL's AXI master DMAs straight into that last range, so it must be real QEMU
RAM that Linux has been told not to touch. `CONFIG_STRICT_DEVMEM` is disabled so
the UMD can `mmap` it through `/dev/mem`.

**Module load order matters**: `esp_cache` and `contig_alloc` export symbols that
`esp` needs, and `esp` exports symbols `gt_vortex_rtl` needs. `guest/rootfs/init`
loads them in order. At build time the same dependency is expressed with
`KBUILD_EXTRA_SYMBOLS` -- concatenating into `$(M)/Module.symvers` does *not*
work, because modpost overwrites that file.

**The ESP cache flushes are harmless no-ops here.** `esp_flush()` calls
`esp_private_cache_flush()`/`esp_cache_flush()`, which iterate lists of *probed*
ESP cache devices. There is no such device in the QEMU device tree, so the lists
are empty. `esp_cache.ko` still has to be loaded for the exported symbols.

## Corrected: MMIO endianness

The QEMU device was originally `DEVICE_BIG_ENDIAN`, on the premise that the KMD's
`iowrite32be()`/`ioread32be()` byte-swap. **On RISC-V they do not.**
`soft/common/drivers/common/include/esp_cache.h` contains:

```c
#ifdef __riscv
    #undef  ioread32be
    #define ioread32be ioread32
    #undef  iowrite32be
    #define iowrite32be iowrite32
#endif
```

and that header is reached from every ESP driver via `esp.h`, after `asm/io.h`.
So a RISC-V guest issues ordinary little-endian MMIO, and the device region is
now `DEVICE_LITTLE_ENDIAN` to match. A SPARC/LEON3 guest would want
`DEVICE_BIG_ENDIAN` again. The fix belongs in the model, not in `esp_cache.h` --
the drivers are the DUT.

`selftest/qtest_launch.py` was updated in the same way: its `be32()` helper
became `reg32()`, an identity function on RISC-V.

## Suggested next steps

1. Repeat-launch robustness: the acceptance runs do one launch per boot. Verify
   back-to-back launches in a single boot, and that a watchdog expiry
   (`ESP_VORTEX_BRIDGE_TIMEOUT`) leaves the guest recoverable rather than wedged.
2. Reset/replay: `esp_vortex_bridge_reset()` exists but is not exercised by the
   guest path. A fuzzing campaign needs deterministic reset between cases.
3. Coverage: `CONFIG_KCOV` is enabled in the guest kernel but nothing consumes it
   yet. Joint RTL + driver coverage is still to be built.
4. Timing: QEMU time and RTL time are decoupled -- the RTL completes
   "instantaneously" from the guest's point of view while the vCPU blocks in
   `wait_for_completion_interruptible()`. That is fine for functional and
   security testing, wrong for performance work. If timing matters later, see how
   Xilinx remote-port and SimBricks synchronize simulator clocks.

## Register behavior required by the unchanged ESP KMD

Common ESP offsets:

```text
0x00 CMD: write 1 to run; IRQ handler writes 0 after completion
0x0c page-table physical address
0x10 number of contiguous chunks
0x14 log2 chunk size
0x18 maximum chunk count; must read nonzero
0x20 coherence mode
0x24 peer-to-peer configuration
0x28 source offset
0x2c destination offset
0x34 multicast configuration
0x180 and above: ESP tile-coordinate table
```

GT_VORTEX wrapper offsets:

```text
0x50 physical base added to GPU AXI addresses
0x54 Vortex start/reset pulse
0x58 Vortex busy status
0x60 kernel entry low 32 bits
0x64 kernel entry high 32 bits
0x68 argument address low 32 bits
0x6c argument address high 32 bits
0x70 performance monitor class
```

The launch order in the real driver is roughly:

```text
write common ESP transfer/page-table registers
write Vortex base, entry, argument and performance registers
pulse Vortex start at 0x54
write CMD=1 at 0x00
sleep in wait_for_completion_interruptible()
receive IRQ when wrapper finishes
read busy at 0x58; busy must be zero
write CMD=0 and complete the ioctl
```

## Guest software details and cautions

The current ESP runtime defaults to:

```text
/dev/gt_vortex_rtl.0
/dev/contig_alloc
/dev/mem
VORTEX_ESP_BASE_ADDR=0xA5000000
```

The guest needs all three device paths. The UMD currently uses `/dev/mem` for
the GPU memory window and the contiguous allocator for the handle consumed by
the common ESP driver. Do not simplify this to only one character device
without documenting and implementing a deliberate driver/runtime adaptation.

The guest now uses its own Linux 6.6.156 tree under `guest/`, so the pinned ESP
Linux submodule is no longer on the critical path. It still shows 13 deleted
files left by a prior case-insensitive macOS checkout. They are documented in
`GPU_FUZZING_HANDOFF.md` as filename-collision artifacts, but they have not
been restored in this session. Inspect before restoring; preserve any real user
changes. Current status includes deleted uppercase netfilter headers/sources and
one litmus test.

The Vortex submodule contains many generated and pre-existing untracked files,
including build artifacts and integration-added files. Do not run broad clean,
reset, or deletion commands there.

## Acceptance test (original specification)

Kept for reference; **this now passes** -- see "Acceptance test result" above and
run it with `guest/run-guest.sh --auto`. The guest paths below are what
`guest/rootfs/run-test.sh` checks and executes:

```sh
ls -l /dev/gt_vortex_rtl.0 /dev/contig_alloc /dev/mem
ls -l /lib/libvortex.so /lib/libvortex-esp.so
ls -l /applications/test/gt_vortex_rtl_vortex_basic.exe
ls -l /applications/test/vortex_kernels/basic.vxbin

export VORTEX_DRIVER=esp
/applications/test/gt_vortex_rtl_vortex_basic.exe \
  -k /applications/test/vortex_kernels/basic.vxbin \
  -t 1 -n 1
echo "exit status: $?"
```

Success requires all of the following, all of which were observed:

- Application prints `Test PASSED` and returns zero.
- Driver trace proves the real launch ioctl and completion IRQ ran.
- RTL trace proves wrapper launch, Vortex memory traffic, and completion.
- No mock completion path is involved.
- Exact revisions, configuration, commands, logs, and runtime are preserved.
