# Builds the ESP Linux kernel modules for the QEMU 'virt' guest.
#
# Source is copied from the pinned ESP tree and minimally ported to Linux 6.x;
# see esp-6.6-port.patch for the exact delta. Build order matters: esp.ko needs
# esp_cache.ko's exported symbols, and gt_vortex_rtl.ko needs esp.ko's.

MODDIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
GUEST    := $(abspath $(MODDIR)/..)
WORKSPACE:= $(abspath $(GUEST)/..)
ESP_ROOT ?= $(WORKSPACE)/esp

KSRC        ?= $(GUEST)/linux-6.6.156
DESIGN_PATH ?= $(GUEST)/esp-soc
ARCH        ?= riscv
CROSS_COMPILE ?= riscv64-linux-gnu-

DRIVERS := $(ESP_ROOT)/soft/common/drivers/linux
ACC_SW  := $(ESP_ROOT)/accelerators/rtl/gt_vortex_rtl/sw

MODULES := esp_cache contig_alloc esp gt_vortex_rtl

KBUILD_ARGS := ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
               DRIVERS=$(DRIVERS) ACC_SW=$(ACC_SW) DESIGN_PATH=$(DESIGN_PATH)

all: $(MODULES)

# esp.ko and gt_vortex_rtl.ko reference symbols exported by earlier modules, so
# each build is handed the earlier Module.symvers via KBUILD_EXTRA_SYMBOLS.
# (Concatenating into $(M)/Module.symvers does not work: modpost overwrites it.)
esp_cache:
	$(MAKE) -C $(KSRC) M=$(MODDIR)/esp_cache $(KBUILD_ARGS) modules

contig_alloc:
	$(MAKE) -C $(KSRC) M=$(MODDIR)/contig_alloc $(KBUILD_ARGS) modules

esp: esp_cache contig_alloc
	$(MAKE) -C $(KSRC) M=$(MODDIR)/esp $(KBUILD_ARGS) modules \
	    KBUILD_EXTRA_SYMBOLS="$(MODDIR)/esp_cache/Module.symvers $(MODDIR)/contig_alloc/Module.symvers"

gt_vortex_rtl: esp
	$(MAKE) -C $(KSRC) M=$(MODDIR)/gt_vortex_rtl $(KBUILD_ARGS) modules \
	    KBUILD_EXTRA_SYMBOLS="$(MODDIR)/esp/Module.symvers"

clean:
	for m in $(MODULES); do $(MAKE) -C $(KSRC) M=$(MODDIR)/$$m $(KBUILD_ARGS) clean; done

patch:
	cd $(MODDIR) && diff -ruN --exclude='*.o' --exclude='*.ko' --exclude='*.mod*' \
	    --exclude='.*' --exclude='Module.symvers' --exclude='modules.order' \
	    .orig . > esp-6.6-port.patch || true
	@echo "wrote esp-6.6-port.patch"

.PHONY: all clean patch $(MODULES)
