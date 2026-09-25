# Vortex GPU test programs, run through QEMU + Linux + the ESP driver + Vortex RTL.
#
#   make              build both programs and the guest image build/initramfs.cpio.gz
#   make run          boot the guest, run both tests, power off   (16 elements)
#   make run N=256    same, with 256 elements
#   make shell        boot, run both tests, then stay in the guest shell (Ctrl-A X quits)
#   make clean        delete everything built
#
# Inside the guest shell:
#   vector_addition -n 64
#   vector_multiplication -n 64
#   N=256 /run-test.sh

TOP       := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WORKSPACE := $(abspath $(TOP)/..)
PROGRAMS  := vector_addition vector_multiplication
N         ?= 16

ROOTFS := $(TOP)/build/rootfs
IMAGE  := $(TOP)/build/initramfs.cpio.gz

all: image

# Build each program (sequentially: vxbin.py uses a fixed temp file).
programs:
	@for p in $(PROGRAMS); do $(MAKE) --no-print-directory -C $(TOP)/$$p || exit 1; done

# Start from a copy of the working guest file tree, add our programs, pack it.
image: programs
	rm -rf $(ROOTFS)
	mkdir -p $(TOP)/build
	cp -a $(WORKSPACE)/guest/rootfs $(ROOTFS)
	@for p in $(PROGRAMS); do \
	    echo "  install $$p -> /bin/$$p, /applications/test/vortex_kernels/$$p.vxbin"; \
	    cp $(TOP)/$$p/build/$$p.exe   $(ROOTFS)/bin/$$p; \
	    cp $(TOP)/$$p/build/$$p.vxbin $(ROOTFS)/applications/test/vortex_kernels/; \
	done
	cp $(TOP)/guest/run-test.sh $(ROOTFS)/run-test.sh
	chmod +x $(ROOTFS)/run-test.sh
	cd $(ROOTFS) && find . -print0 | cpio --null -o --format=newc --quiet | gzip -9 > $(IMAGE)
	@echo "  image: $(IMAGE)"

run: image
	INITRD=$(IMAGE) $(WORKSPACE)/guest/run-guest.sh --auto -n $(N)

shell: image
	INITRD=$(IMAGE) $(WORKSPACE)/guest/run-guest.sh -n $(N)

clean:
	@for p in $(PROGRAMS); do $(MAKE) --no-print-directory -C $(TOP)/$$p clean; done
	rm -rf $(TOP)/build

.PHONY: all programs image run shell clean
