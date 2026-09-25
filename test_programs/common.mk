# Shared build rules for the Vortex test programs.
# Each program's Makefile sets NAME and includes this file.
#
# Every program is TWO programs:
#   kernel.cpp -> build/$(NAME).vxbin   the GPU program (bare-metal RISC-V, runs on Vortex)
#   main.cpp   -> build/$(NAME).exe     the CPU program (RISC-V Linux, runs in the guest)

WORKSPACE := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
VORTEX    := $(WORKSPACE)/esp/accelerators/third-party/GT_VORTEX/vortex
TOOLDIR   ?= $(HOME)/tools
GUEST_LIB := $(WORKSPACE)/guest/rootfs/lib

# ---- GPU side: bare-metal RISC-V, because there is no OS on the GPU ----------
GPU_PREFIX   := $(TOOLDIR)/riscv64-gnu-toolchain/bin/riscv64-unknown-elf-
GPU_CXX      := $(GPU_PREFIX)g++
GPU_OBJCOPY  := $(GPU_PREFIX)objcopy
GPU_OBJDUMP  := $(GPU_PREFIX)objdump
# Address the GPU program is linked at; the UMD reads it back from the .vxbin header.
STARTUP_ADDR ?= 0x1000

GPU_CFLAGS  := -march=rv64imafd -mabi=lp64d -O3 -mcmodel=medany \
               -fno-rtti -fno-exceptions -nostartfiles -nostdlib \
               -fdata-sections -ffunction-sections \
               -I$(VORTEX)/kernel/include -I$(VORTEX)/hw -DXLEN_64 -DNDEBUG
GPU_LDFLAGS := -Wl,-Bstatic,--gc-sections,-T,$(VORTEX)/kernel/scripts/link64.ld,--defsym=STARTUP_ADDR=$(STARTUP_ADDR) \
               $(VORTEX)/kernel/libvortex.a \
               -L$(TOOLDIR)/libc64/lib -lm -lc \
               $(TOOLDIR)/libcrt64/lib/baremetal/libclang_rt.builtins-riscv64.a

# ---- CPU side: a normal RISC-V Linux program for the guest -------------------
HOST_CXX      := riscv64-linux-gnu-g++
HOST_CXXFLAGS := -std=c++11 -O2 -Wall -Wextra -DXLEN_64 \
                 -I$(VORTEX)/runtime/include -I$(VORTEX)/hw -I.
# Link against the guest's own libvortex.so (the UMD) and tell the guest's
# dynamic loader to look for it in /lib.
HOST_LDFLAGS  := -L$(GUEST_LIB) -lvortex -Wl,-rpath,/lib

BUILD := build

all: $(BUILD)/$(NAME).exe $(BUILD)/$(NAME).vxbin $(BUILD)/kernel.dump

$(BUILD)/kernel.elf: kernel.cpp common.h | $(BUILD)
	$(GPU_CXX) $(GPU_CFLAGS) kernel.cpp $(GPU_LDFLAGS) -o $@

# .vxbin = 8-byte min address + 8-byte max address + the raw memory image.
$(BUILD)/$(NAME).vxbin: $(BUILD)/kernel.elf
	OBJCOPY=$(GPU_OBJCOPY) python3 $(VORTEX)/kernel/scripts/vxbin.py $< $@

$(BUILD)/kernel.dump: $(BUILD)/kernel.elf
	$(GPU_OBJDUMP) -d $< > $@

$(BUILD)/$(NAME).exe: main.cpp common.h | $(BUILD)
	$(HOST_CXX) $(HOST_CXXFLAGS) main.cpp $(HOST_LDFLAGS) -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean
