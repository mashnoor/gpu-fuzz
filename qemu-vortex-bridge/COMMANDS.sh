#!/usr/bin/env bash
set -euo pipefail

# Run from any directory. This file records the verified bring-up sequence.
WORKSPACE=/home/nowfel/lab-works/fuzzing/gpu_fuzz
VORTEX_DIR="$WORKSPACE/esp/accelerators/third-party/GT_VORTEX/vortex"
QEMU_DIR="$WORKSPACE/qemu-v8.2.2"
QEMU_BUILD="$QEMU_DIR/build-riscv64"
QEMU_BIN="$QEMU_BUILD/qemu-system-riscv64"
BRIDGE_DIR="$WORKSPACE/qemu-vortex-bridge"
BRIDGE_LIB="$BRIDGE_DIR/libesp_vortex_bridge.so"

# 1. Build and verify the standalone Vortex RTL path.
cd "$VORTEX_DIR"
./configure --xlen=64 --tooldir=/home/nowfel/tools
git submodule update --init --depth 1 third_party/fpnew third_party/ramulator third_party/softfloat
make -C hw
make -C third_party softfloat
make -C third_party ramulator
make -C runtime/rtlsim -j4
make -C kernel -j4
make -C tests/regression/basic clean-kernel
make -C tests/regression/basic STARTUP_ADDR=0x1000 all -j4
make -C tests/regression/basic STARTUP_ADDR=0x1000 run-rtlsim OPTS="-n1"

# 2. Configure and build the patched QEMU RISC-V emulator.
sudo apt-get install -y libglib2.0-dev libpixman-1-dev zlib1g-dev ninja-build
cd "$QEMU_DIR"
mkdir -p "$QEMU_BUILD"
cd "$QEMU_BUILD"
../configure --target-list=riscv64-softmmu --disable-werror --enable-debug
ninja qemu-system-riscv64

# 3. Confirm that the ESP/Vortex device type was compiled into QEMU.
#    NOTE: `-device help` does NOT list it. esp-vortex is a sysbus device that
#    the virt machine instantiates itself, and sysbus devices are not
#    user-creatable, so they never appear in `-device help`. Query QOM instead.
printf '{"execute":"qmp_capabilities"}\n{"execute":"qom-list-types"}\n{"execute":"quit"}\n' \
  | "$QEMU_BIN" -M virt -m 256M -display none -serial none -qmp stdio -S 2>/dev/null \
  | tr '{' '\n' | grep -i vortex
# expected: "name": "esp-vortex", "parent": "sys-bus-device"

# 4. Confirm the device-tree node the ESP KMD binds to.
cd "$(mktemp -d)"
"$QEMU_BIN" -M virt,dumpdtb=virt.dtb -m 1G -nographic
fdtdump virt.dtb | grep -A5 'gt-vortex'
# expected: gt-vortex@10010000, compatible "GATech,gt_vortex",
#           reg = <0x0 0x10010000 0x0 0x200>, interrupts = <0xc>

# 5. Lint the ESP wrapper plus Vortex RTL under Verilator (Verilator 5.034 here).
#    MEM_ADDR_WIDTH=32 is required: the ESP socket gives GT_VORTEX_wrapper a
#    32-bit AXI address bus, but VX_config.vh defaults MEM_ADDR_WIDTH to 48
#    under XLEN_64, which trips Vortex_axi's STATIC_ASSERT and truncates
#    addresses at the wrapper port.
make -C "$BRIDGE_DIR" lint

# 6. Build libesp_vortex_bridge.so: the Verilated GT_VORTEX_wrapper that QEMU
#    dlopen()s. Takes roughly 10 minutes.
make -C "$BRIDGE_DIR"
nm -D --defined-only "$BRIDGE_LIB" | grep esp_vortex_bridge
# expected: create/destroy/reset/read/write/run

# 7. Smoke-test the bridge on the host, with libvortex-bridge.so standing in for
#    QEMU (it owns the "guest RAM" and issues the same register sequence as
#    gt_vortex_rtl.c). This exercises the real RTL but NOT the Linux KMD.
make -C "$BRIDGE_DIR/selftest"
export LD_LIBRARY_PATH="$VORTEX_DIR/runtime:$BRIDGE_DIR/selftest:$BRIDGE_DIR"
export VORTEX_DRIVER=bridge
export VORTEX_BRIDGE_LIB="$BRIDGE_LIB"
cd "$VORTEX_DIR/tests/regression/basic"
./basic -t 1 -n 1    # expected: Test PASSED, exit status 0
./basic -t 1 -n 64   # expected: Test PASSED, exit status 0

# 8. Capture that launch and replay it through the QEMU device model, checking
#    guest-RAM DMA and PLIC interrupt delivery. Still not the acceptance test:
#    there is no Linux kernel and no ESP KMD in this path.
export VORTEX_BRIDGE_DUMP=/tmp/vortex-launch.txt
./basic -t 1 -n 8
unset VORTEX_BRIDGE_DUMP VORTEX_DRIVER LD_LIBRARY_PATH
export ESP_VORTEX_BRIDGE_LIB="$BRIDGE_LIB"
python3 "$BRIDGE_DIR/selftest/qtest_launch.py" /tmp/vortex-launch.txt --qemu "$QEMU_BIN"
# expected: PLIC IRQ 12 pending, VX_BUSY=0, result regions match, exit status 0

# Useful bridge environment variables:
#   ESP_VORTEX_BRIDGE_LIB      path to libesp_vortex_bridge.so (required by QEMU)
#   ESP_VORTEX_BRIDGE_VERBOSE  log every APB write and the cycle/DMA counts
#   ESP_VORTEX_BRIDGE_TIMEOUT  execution watchdog in RTL cycles (default 200e6)
#   ESP_VORTEX_BRIDGE_VCD      VCD path; requires `make TRACE=1` in $BRIDGE_DIR

GUEST="$WORKSPACE/guest"

# 9. Build the RISC-V Linux guest kernel (6.6 LTS, not ESP's pinned 5.1: 5.1
#    does not build with GCC 13 and has no ARCH_HAS_KCOV on RISC-V, which the
#    driver-side fuzzing goal needs).
cd "$GUEST"
curl -O https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.156.tar.xz
tar xf linux-6.6.156.tar.xz
cd linux-6.6.156
export ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu-
make defconfig
./scripts/config --enable MODULES --enable MODULE_UNLOAD --enable DEVMEM \
                 --disable STRICT_DEVMEM --disable IO_STRICT_DEVMEM \
                 --enable DEVTMPFS --enable DEVTMPFS_MOUNT \
                 --enable BLK_DEV_INITRD --enable KCOV --enable DEBUG_FS
make olddefconfig
make -j$(nproc) Image modules

# 10. Build the four ESP kernel modules against that kernel. Sources are copied
#     out of the pinned ESP tree into guest/esp-modules and minimally ported;
#     `make patch` regenerates esp-6.6-port.patch showing the exact delta
#     (currently two lines: class_create() lost its owner argument in 6.4).
#     guest/esp-soc/socgen/esp/soc_defs.h is a hand-written stand-in for the
#     header ESP normally generates from an esp-xconfig SoC description.
make -C "$GUEST/esp-modules"
find "$GUEST/esp-modules" -name '*.ko'

# 11. Cross-compile the user-mode stack for riscv64 into the guest rootfs.
cd "$WORKSPACE"
make -C "$VORTEX_DIR/runtime/stub" DESTDIR="$GUEST/rootfs/lib" CXX=riscv64-linux-gnu-g++
make -C "$VORTEX_DIR/runtime/esp"  DESTDIR="$GUEST/rootfs/lib" CXX=riscv64-linux-gnu-g++ \
     ESP_ROOT="$WORKSPACE/esp" DESIGN_PATH="$GUEST/esp-soc"
riscv64-linux-gnu-g++ -std=c++11 -O2 -DNDEBUG -DXLEN_64 \
    -I"$VORTEX_DIR/runtime/include" -I"$VORTEX_DIR/hw" \
    "$VORTEX_DIR/tests/regression/basic/main.cpp" \
    -L"$GUEST/rootfs/lib" -lvortex -Wl,-rpath,/lib \
    -o "$GUEST/rootfs/applications/test/gt_vortex_rtl_vortex_basic.exe"
cp "$VORTEX_DIR/tests/regression/basic/kernel.vxbin" \
   "$GUEST/rootfs/applications/test/vortex_kernels/basic.vxbin"

# 12. Repack the initramfs after changing anything under guest/rootfs.
cd "$GUEST/rootfs" && find . -print0 | cpio --null -o --format=newc | gzip -9 > ../initramfs.cpio.gz

# 13. ACCEPTANCE TEST: boot the guest and run the workload through the real
#     UMD -> KMD -> MMIO -> RTL -> DMA -> interrupt path.
"$GUEST/run-guest.sh" --auto            # or: --auto -n 64, or --verbose
# expected: "Test PASSED", "exit status: 0", and the PLIC "esp" interrupt
# counter incrementing from 0 to 1 across the launch.
