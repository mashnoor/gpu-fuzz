# Builds libesp_vortex_bridge.so: the Verilated ESP GT_VORTEX_wrapper driven by
# QEMU's esp-vortex device model.
#
#   make            build the bridge library
#   make TRACE=1    build with VCD tracing (set ESP_VORTEX_BRIDGE_VCD at run time)
#   make DEBUG=1    unoptimized build with symbols

SHELL := /bin/bash

BRIDGE_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
WORKSPACE    := $(abspath $(BRIDGE_DIR)/..)
ESP_ROOT     ?= $(WORKSPACE)/esp
GT_VORTEX    ?= $(ESP_ROOT)/accelerators/third-party/GT_VORTEX
VORTEX_HOME  ?= $(GT_VORTEX)/vortex
TOOLDIR      ?= /home/nowfel/tools
VERILATOR    ?= $(TOOLDIR)/verilator/bin/verilator

RTL_DIR      := $(VORTEX_HOME)/hw/rtl
DPI_DIR      := $(VORTEX_HOME)/hw/dpi
SIM_DIR      := $(VORTEX_HOME)/sim
COMMON_DIR   := $(SIM_DIR)/common
THIRD_PARTY  := $(VORTEX_HOME)/third_party

XLEN         ?= 64
# The ESP socket presents a 32-bit AXI address bus, so Vortex's memory address
# width must be narrowed to match (the XLEN_64 default of 48 would be truncated
# at the wrapper port).
MEM_ADDR_WIDTH ?= 32

GT_VORTEX_NUM_CORES   ?= 1
GT_VORTEX_NUM_WARPS   ?= 4
GT_VORTEX_NUM_THREADS ?= 4

CONFIGS := -DXLEN_$(XLEN)
CONFIGS += -DMEM_ADDR_WIDTH=$(MEM_ADDR_WIDTH)
CONFIGS += -DESP_GT_VORTEX_NUM_CORES=$(GT_VORTEX_NUM_CORES)
CONFIGS += -DESP_GT_VORTEX_NUM_WARPS=$(GT_VORTEX_NUM_WARPS)
CONFIGS += -DESP_GT_VORTEX_NUM_THREADS=$(GT_VORTEX_NUM_THREADS)

TARGET := $(BRIDGE_DIR)/libesp_vortex_bridge.so

CXXFLAGS := -std=c++17 -fPIC -Wall -Wextra -Wno-array-bounds -Wno-maybe-uninitialized
CXXFLAGS += -I$(BRIDGE_DIR) -I$(VORTEX_HOME)/hw -I$(COMMON_DIR)
CXXFLAGS += -I$(THIRD_PARTY)/softfloat/source/include
CXXFLAGS += $(CONFIGS)

LDFLAGS := -shared -pthread
LDFLAGS += $(THIRD_PARTY)/softfloat/build/Linux-x86_64-GCC/softfloat.a

RTL_INCLUDE := -I$(GT_VORTEX) -I$(RTL_DIR) -I$(DPI_DIR) -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces
RTL_INCLUDE += -I$(RTL_DIR)/core -I$(RTL_DIR)/mem -I$(RTL_DIR)/cache -I$(RTL_DIR)/fpu

TOP := GT_VORTEX_wrapper

VL_FLAGS := --exe --cc --top-module $(TOP)
VL_FLAGS += --language 1800-2009 --assert
VL_FLAGS += -Wno-fatal
VL_FLAGS += --x-initial unique --x-assign unique
VL_FLAGS += $(SIM_DIR)/rtlsim/verilator.vlt
VL_FLAGS += -DSIMULATION -DSV_DPI
VL_FLAGS += $(CONFIGS)
VL_FLAGS += $(RTL_INCLUDE)
VL_FLAGS += -j $(shell nproc)

ifdef TRACE
	VL_FLAGS += --trace --trace-structs
endif

ifdef DEBUG
	VL_FLAGS += --trace --trace-structs
	CXXFLAGS += -g -O0
else
	VL_FLAGS += -DNDEBUG
	CXXFLAGS += -O2 -DNDEBUG
endif

SRCS := $(BRIDGE_DIR)/esp_vortex_bridge.cpp
SRCS += $(COMMON_DIR)/util.cpp
SRCS += $(COMMON_DIR)/rvfloats.cpp
SRCS += $(DPI_DIR)/util_dpi.cpp
SRCS += $(DPI_DIR)/float_dpi.cpp

RTL_SRCS := $(GT_VORTEX)/GT_VORTEX_wrapper.v

all: $(TARGET)

$(TARGET): $(SRCS) $(BRIDGE_DIR)/esp_vortex_bridge.h $(RTL_SRCS)
	$(VERILATOR) --build $(VL_FLAGS) $(RTL_SRCS) $(SRCS) \
		-CFLAGS '$(CXXFLAGS)' -LDFLAGS '$(LDFLAGS)' \
		--Mdir $(TARGET).obj_dir -o $(TARGET)

lint:
	$(VERILATOR) --lint-only --top-module $(TOP) --Mdir $(BRIDGE_DIR)/lint_obj \
		-Wno-fatal --language 1800-2009 \
		-DSIMULATION -DSV_DPI $(CONFIGS) $(RTL_INCLUDE) $(RTL_SRCS)

clean:
	rm -rf $(TARGET).obj_dir $(TARGET) $(BRIDGE_DIR)/lint_obj

.PHONY: all lint clean
