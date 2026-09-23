# GPU fuzzing: ESP/Vortex session handoff

Last updated: 2026-09-10. Paths below are relative to the directory containing this file unless stated otherwise.

Latest continuation: a Linux machine is now accessible over SSH. See [Linux preflight and Verilator commands](ESP_VORTEX_LINUX_CHECKS.md) and the remote-work section below. Environment inspection has run; the proposed lint/build/acceptance checks have not.

## Read this first

The user wants to build a research prototype that fuzzes across a real software/hardware stack:

`application → user-space runtime → Linux kernel-mode driver (KMD) → hardware interface → actual GPU RTL`

A user-space runtime connected directly to an RTL simulator is NOT sufficient. The user has no AMD GPU and does not require AMD; open-source GPU hardware is acceptable. Do not assume access to an FPGA or commercial simulator. Prioritize a hardware-free, open-source implementation unless the user specifies otherwise.

ESP with its experimental Vortex integration is the current candidate. Source inspection confirms reusable runtime, Linux KMD, shared driver framework, wrapper RTL, and tests. It does NOT establish a working full-stack simulation environment. No build, Linux boot, or end-to-end test has been run in this investigation.

Next milestone: make one known GPU workload run through the real Linux driver into actual RTL, return through an interrupt, and verify its output. Do not start a large fuzzing campaign before this works.

This document records context and proposed next steps; it does not authorize destructive operations, purchases, deployment, or arbitrary system-wide installations. Follow the current user's instructions and any applicable AGENTS.md files.

## Repository and exact revisions

Main repository: https://github.com/sld-columbia/esp

Branch: `vortex-2.2-integration` (development integration, not the ordinary release branch).

| Repository/component | Revision inspected |
|---|---|
| ESP | `ed7ed2e3daba852c2bc89c9b74c87053c53181ec` |
| `accelerators/third-party/GT_VORTEX/vortex` | `ecf734d632644dc3726c2bfab76d463ff505ffa5` |
| `rtl/cores/ariane/ariane` | `21ee2341cd91637ffe2039cfa17a5790c02c4cdb` |
| `soft/ariane/linux` | `b842fcc582e077fd063f43393cc25488c8c247e9` |
| `soft/ariane/opensbi` | `3a8ec50bcc94fc72b48f6911cf3a11fa2e1673d8` |
| `soft/ariane/riscv-pk` | `09b8397bfab576398a7bb6e5000e0ef71eeb1215` |

The Vortex submodule is the ESP fork at https://github.com/sld-columbia/vortex, not an arbitrary latest upstream Vortex checkout. Keep the pinned versions together initially.

### Existing workspace

Original location: `/Users/mashnoor/lab_works/fuzzing/gpu_fuzz`.

- `esp/`: newly cloned ESP checkout. The five submodules listed above were initialized at their pinned commits. Other ESP submodules and nested dependencies remain uninitialized.
- `vortex/`: separate pre-existing user repository; do not overwrite it or substitute it for ESP's pinned fork. It has its own AGENTS.md and may contain user changes.
- `fuzzgpu-artifact/`: pre-existing research artifact. Read its applicable AGENTS.md before working there. Its presence does not imply it is already integrated with ESP.
- `gpu_full_stack_fuzzing.svg`: conceptual architecture diagram. Its QEMU–Verilator bridge is PROPOSED, not an existing ESP feature.

No ESP source files were intentionally edited. The original host is macOS ARM64 on a case-insensitive filesystem. Verilator was on PATH; QEMU RISC-V, GHDL, the checked commercial simulator commands, and `riscv64-linux-gnu-gcc` were not found on PATH. This was not an exhaustive tool installation inventory.

IMPORTANT: Linux's checkout has case-colliding filenames (for example `xt_DSCP.h` versus `xt_dscp.h`). On the original filesystem, 13 Linux files appear modified immediately after checkout. These are filesystem collisions, not intentional patches. Do not blindly reset or copy this working tree into a build. Use a fresh checkout on a case-sensitive Linux filesystem. A Linux container bind-mounted onto the same case-insensitive host directory does not solve this.

### Recreate on a new machine

Run in a chosen project directory on a case-sensitive filesystem, only if `esp/` does not already exist:

```sh
git clone --branch vortex-2.2-integration --single-branch https://github.com/sld-columbia/esp.git esp
cd esp
git checkout --detach ed7ed2e3daba852c2bc89c9b74c87053c53181ec
git submodule update --init --depth 1 \
  accelerators/third-party/GT_VORTEX/vortex \
  rtl/cores/ariane/ariane \
  soft/ariane/linux \
  soft/ariane/opensbi \
  soft/ariane/riscv-pk
git status --short
git submodule status
```

This reproduces the inspected sources, NOT a complete build environment. Inspect `.gitmodules` and initialize additional/nested dependencies needed for the selected execution path. Avoid fetching every unrelated HLS/accelerator dependency without reason. If reusing an existing checkout, inspect its status before changing commits.

### Linux remote-work environment (latest update)

The user supplied `nowfel@lab-computer` with project directory `/home/nowfel/lab-works/fuzzing/gpu_fuzz`. Files are continuously synced from the original workspace. The user authorized generating files locally and running build commands on the Linux machine to test the proposed approach.

- SSH access was tested successfully. Host is Linux x86_64, with 24 reported logical CPUs and approximately 29 GiB RAM.
- Verilator 5.034 exists at `/home/nowfel/tools/verilator/bin/verilator`, but was not on the default SSH PATH.
- Bare-metal RV64 GCC exists at `/home/nowfel/tools/riscv64-gnu-toolchain/bin/riscv64-unknown-elf-gcc`. This is not a Linux userspace cross-compiler.
- `g++`, `clang++`, Make, CMake, and Docker were found. `rg` was unavailable remotely; use available alternatives or arrange a scoped installation if needed.
- QEMU RISC-V, GHDL, Linux cross-GCC, and commercial simulator commands were not found on the checked PATH. Do not infer their complete absence without further inspection.
- The synced ESP tree is present. Its Vortex submodule was clean when checked; the Linux submodule still reports modifications from the macOS filename-collision issue. Do not reset it blindly. Use a fresh Linux-local checkout outside the synced tree for kernel builds.
- Keep generated build outputs outside the synced source tree to avoid cross-machine build artifacts and concurrent modifications. The runbook uses a unique `/tmp/esp-vortex-check.*` directory for lint output; preserve useful logs before reboot.

Next experiment: run the commands in [ESP_VORTEX_LINUX_CHECKS.md](ESP_VORTEX_LINUX_CHECKS.md) to test standalone Verilator elaboration of `GT_VORTEX_wrapper` and Vortex. The command is exploratory and has not been executed yet. It uses simulation/DPI defines; successful lint is not an executable build, proof of FPGA FPU compatibility, or proof of Linux-driver-to-RTL execution. Record actual results here after running it.

## Source map: what actually exists

All paths in this section are relative to `esp/`.

| Component | Files to read | What was verified |
|---|---|---|
| Integration instructions | `accelerators/rtl/gt_vortex_rtl/README.md`, `accelerators/third-party/GT_VORTEX/README.md` | Linux staging, runtime selection, workloads, configuration requirements |
| User runtime | `accelerators/third-party/GT_VORTEX/vortex/runtime/esp/vortex.cpp` | Opens devices, uploads/downloads memory, constructs descriptors, calls launch ioctl |
| Vortex Linux KMD | `accelerators/rtl/gt_vortex_rtl/sw/linux/driver/gt_vortex_rtl.c` | Registers platform driver; writes launch registers; uses common ESP framework |
| Shared ioctl ABI | `accelerators/rtl/gt_vortex_rtl/sw/linux/include/gt_vortex_rtl.h` | `GT_VORTEX_RTL_IOC_ACCESS` and descriptor layout |
| Common Linux driver | `soft/common/drivers/linux/esp/esp.c` | `copy_from_user`, contiguous-allocation handles, locks, MMIO, interrupt completion |
| GPU wrapper | `accelerators/third-party/GT_VORTEX/GT_VORTEX_wrapper.v` | APB control, AXI memory interface, interrupt, `Vortex_axi` instance |
| Host/kernel test | `accelerators/third-party/GT_VORTEX/vortex/tests/regression/basic/` | Actual GPU execution and CPU-side output comparison |
| Test staging and runner | `accelerators/rtl/gt_vortex_rtl/sw/linux/app/Makefile`, adjacent `vortex-regression` script | Cross-compiles hosts, stages kernels/libraries, selects backend |
| Linux/boot build | `utils/make/ariane.mk` | Linux payload, root filesystem, BBL/OpenSBI targets |
| Simulation build | `utils/make/modelsim.mk`, `utils/make/ariane.mk`, `utils/Makefile` | Mixed-language simulator; default `TEST_PROGRAM` is `systest.exe` |

Useful functions to trace: runtime `start()` → `GT_VORTEX_RTL_IOC_ACCESS` → common `esp_access_ioctl()` → `gt_vortex_prep_xfer()` → wrapper → GPU → common interrupt handler/completion → ioctl returns.

### Driver and memory details

- Runtime device defaults: `/dev/gt_vortex_rtl.0`, `/dev/mem`, `/dev/contig_alloc`.
- Reserved Vortex memory-window default: physical address `0xA5000000`; verify against the chosen guest memory map before use.
- Runtime memory copies use `/dev/mem`; contiguous allocation also participates through its kernel driver. Do not model the system as only one device node or claim all allocation bypasses the kernel.
- Launch fields include memory base, kernel entry address, argument address, start bit, and performance-monitor class.
- Wrapper offsets include base `0x50`, start `0x54`, busy `0x58`, kernel entry `0x60/0x64`, arguments `0x68/0x6c`, and performance class `0x70`.
- The KMD uses `iowrite32be`/`ioread32be`; any bridge must validate byte order, not just forward integers casually.
- The wrapper interface includes a 64-bit AXI memory master, APB control, and completion interrupt. The surrounding ESP socket/common-driver registers also matter; exposing only the Vortex-specific register offsets is not sufficient for an unchanged ESP driver.
- Launch is synchronous/blocking. Runtime `ready_wait()` ignores its timeout argument; common `esp_wait()` uses an interruptible completion wait without a timeout. A host-side watchdog and reliable reset/restart strategy are needed for fuzzing.
- This is an accelerator-style Linux driver, not a production AMD DRM/GEM/VM/scheduler stack. It meets the real-KMD requirement but does not replicate AMD driver complexity or isolation.
- Treat the existing `/dev/mem` interface as privileged research infrastructure; keep experiments isolated from valuable host data.

## Known gaps and pitfalls

1. **No verified Linux-plus-Vortex RTL simulation flow.** ESP's inspected full-system simulation rules use commercial mixed VHDL/SystemVerilog simulators and default to bare-metal software. Official Linux execution instructions use FPGA. No integrated ESP QEMU–Verilator bridge was found in the inspected tree. This is a bounded finding, not proof that Linux simulation is impossible.
2. **CPU simulation is not whole-SoC simulation.** Ariane's own README discusses Verilator and memory preloading, including Linux-boot use cases. Investigate whether these are reusable, but do not infer that the ESP interconnect, peripherals, and Vortex already work in that environment. The `VERILATOR` define in ESP's Xcelium setup is not a native Verilator backend.
3. **Old kernel baseline.** The pinned Linux is 5.1.0. Its RISC-V configuration does not select `ARCH_HAS_KCOV`, while `CONFIG_KCOV` depends on it. Do not promise standard KCOV-based fuzzing without an update/backport or alternate instrumentation. Reassess KASAN independently before enabling it.
4. **Build success can hide missing GPU kernels.** The app Makefile can warn and exit successfully on missing toolchains or failed Vortex software/test builds, then skip missing `.vxbin` files. Check actual artifacts and logs.
5. **Configuration must match.** GPU core/warp/thread counts and L2/L3 settings must agree across hardware, runtime, and kernel binaries. Use the ESP-provided configuration forwarding initially. Checked-in legacy bare-metal byte-array images are single-core examples.
6. **Not a complete fuzzing framework.** Joint software/RTL coverage, corpus management, fault oracles, minimization, timeout handling, and deterministic replay still need implementation after the execution path works.

## Acceptance test

### Preconditions

Run the following INSIDE an ESP-compatible Linux system attached to actual Vortex RTL (simulated or synthesized on FPGA), not on the build host. The runtime, GPU driver, common ESP/contiguous-allocation drivers, and matching kernel binary must be installed. Root access is expected for the current backend.

```sh
ls -l /dev/gt_vortex_rtl.0 /dev/contig_alloc /dev/mem
ls -l /applications/test/gt_vortex_rtl_vortex_basic.exe
ls -l /applications/test/vortex_kernels/basic.vxbin
ls -l /lib/libvortex.so /lib/libvortex-esp.so
export VORTEX_DRIVER=esp
vortex-regression basic -t 1 -n 1
acceptance_rc=$?
echo "exit status: $acceptance_rc"
```

Equivalent explicit invocation:

```sh
VORTEX_DRIVER=esp /applications/test/gt_vortex_rtl_vortex_basic.exe \
  -k /applications/test/vortex_kernels/basic.vxbin -t 1 -n 1
```

Expected output includes `run kernel test`, `start execution`, `verify result`, and `Test PASSED`, with exit status 0.

Use `-t 1`: this executes the GPU kernel. `-t 0` only checks host/device memory copying and is NOT the full-stack acceptance test. Keep the initial workload small; increasing the size is a later step.

### Evidence required to declare success

- Application output comparison passes and exit status is zero.
- Driver trace shows the real launch ioctl and completion interrupt path.
- RTL waveform/trace shows launch, GPU memory transactions, and completion.
- Preserve exact revisions/configuration, build/run commands, logs, and elapsed runtime.
- Establish repeatable reset/restart for subsequent tests; do not assume killing a userspace process resets the GPU.

A mock device that returns completion without running GPU RTL does not pass. Neither does a standalone user-space Vortex RTL simulator that bypasses Linux KMD.

## Suggested continuation on the new machine

1. Read this file and applicable repository instructions. Inspect host OS/architecture, case sensitivity, available compilers/simulators, existing checkouts, and user changes. Do not assume paths/tools from the old Mac exist.
2. Reproduce the pinned source tree on a case-sensitive filesystem. Read the integration README and key files above.
3. Decide the execution path using evidence:
   - **Full ESP simulation:** investigate Linux payload loading, mixed-language simulation/tool availability, memory map, boot firmware, and execution speed. Do not claim `make sim` boots Linux unchanged.
   - **QEMU Linux + Vortex wrapper in Verilator:** proposed open-source route. Implement register forwarding, GPU memory access to guest RAM, interrupt delivery, reset, and execution synchronization. Preserve/model the ESP socket and allocation/cache behavior required by the real drivers, or explicitly document necessary driver adaptations. Do not assume a generic QEMU `virt` machine matches ESP's addresses/peripherals.
   - **Supported FPGA:** only if hardware/tools are available and the user chooses this route.
4. Before a large implementation, identify the smallest viable topology and write down the guest physical memory map, device registers/endianness, IRQ wiring, and reset protocol. Resolve whether the common ESP dependencies will be preserved or adapted.
5. Bring up Linux and the driver, then execute the acceptance test with actual RTL and collect evidence. Independent RTL smoke tests are useful intermediate checks but not final acceptance.
6. Only after acceptance, add structured workload/ioctl mutation, software and RTL feedback, watchdogs, replay, and minimization.

Do not re-run broad paper searches before addressing the concrete execution-environment gap. If the current user only requests explanation/review, report the proposed implementation instead of silently starting it.

### FPGA flow reference (conditional, not a simulator command)

With a supported VCU118 board, appropriate Vivado installation, Linux build host, toolchains, initialized dependencies, and completed ESP board setup:

```sh
cd esp/socs/xilinx-vcu118-xcvu9p
make esp-xconfig
# Select Ariane and GT_VORTEX, start with one GPU core, save the SoC configuration.
make linux
make vivado-syn
make fpga-program
make fpga-run-linux
```

These are documented high-level targets, not a self-contained installation recipe or locally verified run. An AMD/Xilinx FPGA here is not an AMD Radeon GPU.

## Primary references

- ESP Vortex/third-party integration: https://www.esp.cs.columbia.edu/docs/thirdparty_acc/thirdparty_acc-guide/
- ESP SoC simulation and FPGA/Linux flow: https://www.esp.cs.columbia.edu/docs/singlecore/singlecore-guide/
- ESP setup and tool prerequisites: https://www.esp.cs.columbia.edu/docs/setup/setup-guide/
- Pinned ESP source: https://github.com/sld-columbia/esp/tree/ed7ed2e3daba852c2bc89c9b74c87053c53181ec
- Pinned Vortex fork: https://github.com/sld-columbia/vortex/tree/ecf734d632644dc3726c2bfab76d463ff505ffa5

Online documentation may advance beyond the pinned code. Prefer the pinned source when resolving version-specific behavior.

## Suggested prompt for the next session

> Read GPU_FUZZING_HANDOFF.md and applicable AGENTS.md files. Continue the ESP/Vortex driver-to-RTL prototype on this machine. First verify the environment and pinned sources, then assess and implement the smallest hardware-free Linux-KMD-to-actual-Vortex-RTL execution path. Use the documented basic kernel test as the acceptance criterion. Preserve existing user changes, distinguish implemented features from proposals, and report evidence rather than assuming the full stack works.
