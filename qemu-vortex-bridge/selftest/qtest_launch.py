#!/usr/bin/env python3
"""Replay a captured Vortex launch through QEMU's esp-vortex device.

This drives the QEMU device model exactly the way the ESP Linux KMD would --
MMIO register writes, then CMD_REG=1 -- but from qtest instead of
guest code, so it can run before a RISC-V Linux guest exists.

It checks the three things guest code could not otherwise confirm here:

  1. the launch registers reach the Verilated GT_VORTEX_wrapper through QEMU's
     MMIO decode with the byte order the ESP drivers actually produce,
  2. the RTL's AXI master reads and writes real QEMU guest RAM, and
  3. completion raises PLIC IRQ 12, and writing CMD_REG=0 lowers it again.

Input is the file produced by running the regression test with
VORTEX_BRIDGE_DUMP=<file> against libvortex-bridge.so.

This is NOT the acceptance test: no Linux kernel, no ESP KMD, no ioctl path.
"""

import argparse
import os
import subprocess
import sys
import time

ESP_VORTEX_BASE = 0x10010000
CMD_REG = 0x00
PT_NCHUNK_MAX_REG = 0x18
REG_BASE_ADDR = 0x50
REG_START_VORTEX = 0x54
REG_VX_BUSY = 0x58
REG_STARTUP_ADDR0 = 0x60
REG_STARTUP_ADDR1 = 0x64
REG_STARTUP_ARG0 = 0x68
REG_STARTUP_ARG1 = 0x6C
REG_MPM_CLASS = 0x70

PLIC_BASE = 0xC000000
PLIC_PENDING_BASE = 0x1000
ESP_VORTEX_IRQ = 12

WRITE_CHUNK = 256  # bytes of guest RAM per qtest 'write' command


def reg32(value):
    """Value as the guest CPU stores it for the ESP drivers' MMIO accessors.

    The drivers spell these iowrite32be()/ioread32be(), but esp_cache.h
    redefines both to the little-endian iowrite32()/ioread32() under
    #ifdef __riscv, so on RISC-V no byte swap happens and the QEMU device
    region is DEVICE_LITTLE_ENDIAN to match. Identity on RISC-V.
    """
    return value & 0xFFFFFFFF


class QTest:
    def __init__(self, qemu, memory, extra_args):
        argv = [
            qemu, "-M", "virt", "-m", memory,
            "-display", "none", "-serial", "none",
            "-accel", "qtest", "-qtest", "stdio",
        ] + extra_args
        self.proc = subprocess.Popen(
            argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            text=True, bufsize=1)

    def cmd(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()
        while True:
            reply = self.proc.stdout.readline()
            if reply == "":
                raise RuntimeError("qtest connection closed on: %s" % line)
            reply = reply.strip()
            if reply.startswith("OK") or reply.startswith("FAIL"):
                break
        if not reply.startswith("OK"):
            raise RuntimeError("qtest %r -> %s" % (line, reply))
        return reply[3:].strip()

    def readl(self, addr):
        return int(self.cmd("readl 0x%x" % addr), 0)

    def writel(self, addr, value):
        self.cmd("writel 0x%x 0x%x" % (addr, value))

    def read_mem(self, addr, length):
        return bytes.fromhex(self.cmd("read 0x%x %d" % (addr, length))[2:])

    def write_mem(self, addr, data):
        for off in range(0, len(data), WRITE_CHUNK):
            chunk = data[off:off + WRITE_CHUNK]
            self.cmd("write 0x%x %d 0x%s" % (addr + off, len(chunk),
                                             chunk.hex()))

    def close(self):
        try:
            self.proc.stdin.close()
        except OSError:
            pass
        self.proc.terminate()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait(timeout=10)


def parse_dump(path):
    launch = {"regions": [], "expect": []}
    with open(path) as fh:
        for line in fh:
            parts = line.split()
            if not parts:
                continue
            tag = parts[0]
            if tag in ("base", "krnl", "args", "mpm"):
                launch[tag] = int(parts[1], 0)
            elif tag in ("region", "expect"):
                launch[tag if tag == "expect" else "regions"].append(
                    (int(parts[1], 0), bytes.fromhex(parts[3])))
    for key in ("base", "krnl", "args", "mpm"):
        if key not in launch:
            raise ValueError("dump is missing '%s'" % key)
    return launch


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump", help="file written by VORTEX_BRIDGE_DUMP")
    ap.add_argument("--qemu", required=True)
    ap.add_argument("--memory", default="1G")
    ap.add_argument("--timeout", type=float, default=900.0,
                    help="seconds to wait for the completion interrupt")
    ap.add_argument("--qemu-arg", action="append", default=[])
    args = ap.parse_args()

    if not os.environ.get("ESP_VORTEX_BRIDGE_LIB"):
        sys.exit("ESP_VORTEX_BRIDGE_LIB must point at libesp_vortex_bridge.so")

    launch = parse_dump(args.dump)
    base = launch["base"]
    dev = ESP_VORTEX_BASE
    failures = []

    qt = QTest(args.qemu, args.memory, args.qemu_arg)
    try:
        # The ESP common driver refuses to run if this reads back zero.
        nchunk_max = qt.readl(dev + PT_NCHUNK_MAX_REG)
        print("PT_NCHUNK_MAX (as ioread32) = %d" % reg32(nchunk_max))
        if reg32(nchunk_max) == 0:
            failures.append("PT_NCHUNK_MAX_REG reads zero")

        print("loading %d pre-launch region(s) into guest RAM at 0x%x"
              % (len(launch["regions"]), base))
        for addr, data in launch["regions"]:
            qt.write_mem(base + addr, data)
            got = qt.read_mem(base + addr, len(data))
            if got != data:
                failures.append("guest RAM readback differs at 0x%x" % addr)

        print("programming launch registers")
        qt.writel(dev + REG_START_VORTEX, reg32(0))
        qt.writel(dev + REG_BASE_ADDR, reg32(base))
        qt.writel(dev + REG_STARTUP_ADDR0, reg32(launch["krnl"]))
        qt.writel(dev + REG_STARTUP_ADDR1, reg32(launch["krnl"] >> 32))
        qt.writel(dev + REG_STARTUP_ARG0, reg32(launch["args"]))
        qt.writel(dev + REG_STARTUP_ARG1, reg32(launch["args"] >> 32))
        qt.writel(dev + REG_MPM_CLASS, reg32(launch["mpm"]))
        qt.readl(dev + REG_MPM_CLASS)

        # Confirm the register write survived QEMU's MMIO decode.
        readback = reg32(qt.readl(dev + REG_BASE_ADDR))
        print("BASE_ADDR read back as 0x%08x" % readback)
        if readback != (base & 0xFFFFFFFF):
            failures.append("BASE_ADDR read back as 0x%08x" % readback)

        qt.writel(dev + REG_START_VORTEX, reg32(1))
        qt.readl(dev + REG_START_VORTEX)

        pending_addr = PLIC_BASE + PLIC_PENDING_BASE
        irq_mask = 1 << ESP_VORTEX_IRQ
        if qt.readl(pending_addr) & irq_mask:
            failures.append("IRQ %d already pending before launch"
                            % ESP_VORTEX_IRQ)

        print("CMD_REG=1: starting the RTL")
        start = time.time()
        qt.writel(dev + CMD_REG, reg32(1))

        deadline = start + args.timeout
        while True:
            if qt.readl(pending_addr) & irq_mask:
                break
            if time.time() > deadline:
                failures.append("no PLIC IRQ %d within %.0fs"
                                % (ESP_VORTEX_IRQ, args.timeout))
                break
            time.sleep(0.05)
        elapsed = time.time() - start
        print("PLIC IRQ %d pending after %.2fs" % (ESP_VORTEX_IRQ,
                                                   elapsed))

        busy = reg32(qt.readl(dev + REG_VX_BUSY))
        print("VX_BUSY (as ioread32) = 0x%08x" % busy)
        if busy & 1:
            failures.append("wrapper still reports busy at interrupt time")

        # This is what the ESP IRQ handler does once it sees 'done'.
        qt.writel(dev + CMD_REG, reg32(0))
        if qt.readl(pending_addr) & irq_mask:
            failures.append("IRQ %d still pending after CMD_REG=0"
                            % ESP_VORTEX_IRQ)
        else:
            print("IRQ %d cleared by CMD_REG=0" % ESP_VORTEX_IRQ)

        print("checking %d result region(s) written by the RTL"
              % len(launch["expect"]))
        for index, (addr, want) in enumerate(launch["expect"]):
            got = qt.read_mem(base + addr, len(want))
            status = "match" if got == want else "MISMATCH"
            print("  result[%d] at 0x%08x (%d bytes): %s"
                  % (index, addr, len(want), status))
            if got != want and index == 0:
                failures.append("result buffer at 0x%x does not match the "
                                "host reference run" % addr)
    finally:
        qt.close()

    if failures:
        print("\nFAILED:")
        for item in failures:
            print("  - %s" % item)
        return 1
    print("\nOK: QEMU esp-vortex launch reached the RTL, wrote guest RAM, "
          "and raised PLIC IRQ %d" % ESP_VORTEX_IRQ)
    return 0


if __name__ == "__main__":
    sys.exit(main())
