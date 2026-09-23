# Builds libvortex-bridge.so: a host-side Vortex runtime backend that stands in
# for QEMU so the Verilated GT_VORTEX_wrapper bridge can be smoke-tested without
# a RISC-V Linux guest.

SHELL := /bin/bash

SELFTEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
BRIDGE_DIR   := $(abspath $(SELFTEST_DIR)/..)
WORKSPACE    := $(abspath $(BRIDGE_DIR)/..)
ESP_ROOT     ?= $(WORKSPACE)/esp
VORTEX_HOME  ?= $(ESP_ROOT)/accelerators/third-party/GT_VORTEX/vortex

INC_DIR    := $(VORTEX_HOME)/runtime/include
COMMON_DIR := $(VORTEX_HOME)/runtime/common
SIM_DIR    := $(VORTEX_HOME)/sim

XLEN                  ?= 64
MEM_ADDR_WIDTH        ?= 32
GT_VORTEX_NUM_CORES   ?= 1
GT_VORTEX_NUM_WARPS   ?= 4
GT_VORTEX_NUM_THREADS ?= 4

CONFIGS := -DXLEN_$(XLEN)
CONFIGS += -DMEM_ADDR_WIDTH=$(MEM_ADDR_WIDTH)
CONFIGS += -DESP_GT_VORTEX_NUM_CORES=$(GT_VORTEX_NUM_CORES)
CONFIGS += -DESP_GT_VORTEX_NUM_WARPS=$(GT_VORTEX_NUM_WARPS)
CONFIGS += -DESP_GT_VORTEX_NUM_THREADS=$(GT_VORTEX_NUM_THREADS)

CXXFLAGS := -std=c++17 -fPIC -Wall -Wextra -Wfatal-errors
CXXFLAGS += -I$(INC_DIR) -I$(COMMON_DIR) -I$(VORTEX_HOME)/hw -I$(SIM_DIR)/common
CXXFLAGS += $(CONFIGS) -O2 -DNDEBUG

LDFLAGS := -shared -pthread -ldl

TARGET := $(SELFTEST_DIR)/libvortex-bridge.so

all: $(TARGET)

$(TARGET): $(SELFTEST_DIR)/vortex.cpp $(BRIDGE_DIR)/esp_vortex_bridge.h
	$(CXX) $(CXXFLAGS) $(SELFTEST_DIR)/vortex.cpp $(LDFLAGS) -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
