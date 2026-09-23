#!/usr/bin/env bash
#
# Boot the RISC-V Linux guest and run the ESP/Vortex acceptance test:
#
#   RISC-V Linux app -> libvortex.so + libvortex-esp.so (ESP UMD)
#     -> ioctl on /dev/gt_vortex_rtl.0 -> gt_vortex_rtl.ko + esp.ko (Linux KMD)
#     -> ESP MMIO registers on QEMU's esp-vortex device
#     -> libesp_vortex_bridge.so -> real GT_VORTEX_wrapper/Vortex RTL (Verilator)
#     -> AXI DMA into QEMU guest physical RAM
#     -> busy_interrupt -> PLIC IRQ 12 -> KMD IRQ handler -> ioctl returns
#     -> app verifies the output
#
# Usage:
#   ./run-guest.sh              # boot, run the test, drop to an interactive shell
#   ./run-guest.sh --auto       # boot, run the test, power off (for CI/logs)
#   ./run-guest.sh --auto -n 64 # same, with a 64-element workload
#   ./run-guest.sh --verbose    # also log every APB write and RTL cycle count
#
# Exit the interactive shell with Ctrl-A X.

set -euo pipefail

GUEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE="$(cd "$GUEST_DIR/.." && pwd)"

QEMU="${QEMU:-$WORKSPACE/qemu-v8.2.2/build-riscv64/qemu-system-riscv64}"
KERNEL="${KERNEL:-$GUEST_DIR/linux-6.6.156/arch/riscv/boot/Image}"
INITRD="${INITRD:-$GUEST_DIR/initramfs.cpio.gz}"
BRIDGE="${ESP_VORTEX_BRIDGE_LIB:-$WORKSPACE/qemu-vortex-bridge/libesp_vortex_bridge.so}"

AUTO=0
NPOINTS=1
VERBOSE=0
while [ $# -gt 0 ]; do
    case "$1" in
        --auto)    AUTO=1 ;;
        --verbose) VERBOSE=1 ;;
        -n)        NPOINTS="$2"; shift ;;
        -h|--help) sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
        *)         echo "unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

for f in "$QEMU" "$KERNEL" "$INITRD" "$BRIDGE"; do
    [ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

# QEMU dlopen()s this at machine init; without it the device refuses to launch.
export ESP_VORTEX_BRIDGE_LIB="$BRIDGE"
# RTL execution watchdog, in clock cycles. A real launch takes ~3k-8k cycles;
# this is generous enough that only a genuinely hung design trips it.
export ESP_VORTEX_BRIDGE_TIMEOUT="${ESP_VORTEX_BRIDGE_TIMEOUT:-20000000}"
[ "$VERBOSE" = 1 ] && export ESP_VORTEX_BRIDGE_VERBOSE=1

# Guest memory plan. QEMU virt RAM starts at 0x80000000; -m 2G reaches
# 0x100000000. mem=512M confines Linux to 0x80000000-0xA0000000, leaving the
# contig_alloc pool (0xA0000000) and the Vortex window (0xA5000000) as RAM that
# Linux will not touch but the RTL's AXI master can DMA into.
CMDLINE="console=ttyS0 mem=512M rdinit=/init vxn=$NPOINTS"
[ "$AUTO" = 1 ] && CMDLINE="$CMDLINE autotest"

exec "$QEMU" \
    -M virt -m 2G -nographic \
    -kernel "$KERNEL" \
    -initrd "$INITRD" \
    -append "$CMDLINE"
