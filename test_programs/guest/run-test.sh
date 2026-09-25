#!/bin/sh
# Runs INSIDE the guest. /init calls /run-test.sh once at boot; you can also
# run it again by hand from the guest shell:  N=256 /run-test.sh
export LD_LIBRARY_PATH=/lib VORTEX_DRIVER=esp VORTEX_ESP_BASE_ADDR=0xA5000000

# Element count: N=<n> in the environment, else vxn=<n> on the kernel command line.
[ -z "$N" ] && N=$(sed -n 's/.*vxn=\([0-9]*\).*/\1/p' /proc/cmdline)
[ -z "$N" ] && N=16

rc=0
for t in vector_addition vector_multiplication; do
    echo
    echo "=== $t (n=$N) ==="
    /bin/$t -n "$N" || rc=1
    grep -w esp /proc/interrupts   # GPU completion interrupts so far
done

echo
if [ $rc -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $rc
