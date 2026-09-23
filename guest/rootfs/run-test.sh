#!/bin/sh
# The ESP/Vortex acceptance test, run inside the guest.
export LD_LIBRARY_PATH=/lib
export VORTEX_DRIVER=esp
export VORTEX_ESP_BASE_ADDR=0xA5000000

# workload size: N=<n> in the environment, or vxn=<n> on the kernel cmdline
if [ -z "$N" ]; then
    N=$(sed -n 's/.*vxn=\([0-9]*\).*/\1/p' /proc/cmdline)
fi
[ -z "$N" ] && N=1

echo
echo "=== preconditions ==="
ls -l /dev/gt_vortex_rtl.0 /dev/contig_alloc /dev/mem
ls -l /lib/libvortex.so /lib/libvortex-esp.so
ls -l /applications/test/gt_vortex_rtl_vortex_basic.exe
ls -l /applications/test/vortex_kernels/basic.vxbin

echo
echo "=== PLIC interrupt counters BEFORE the launch ==="
grep -E "esp|CPU" /proc/interrupts

echo
echo "=== running the GPU kernel through the real driver stack ==="
/applications/test/gt_vortex_rtl_vortex_basic.exe \
    -k /applications/test/vortex_kernels/basic.vxbin \
    -t 1 -n "$N"
rc=$?
echo "exit status: $rc"

echo
echo "=== PLIC interrupt counters AFTER the launch ==="
grep -E "esp|CPU" /proc/interrupts
echo "(a non-zero, incremented esp count proves the RTL's completion IRQ"
echo " reached the Linux kernel and woke the driver.)"
exit $rc
