/*
 * soc_defs.h for the QEMU 'virt' stand-in SoC.
 *
 * ESP normally generates this from an esp-xconfig SoC description via
 * tools/socgen/socmap_gen.py:print_soc_defines(). There is no ESP NoC here --
 * QEMU's virt machine has one CPU, one memory, and one accelerator hanging off
 * a flat MMIO bus -- so this describes a 1x1 "SoC" with a single accelerator.
 *
 * Only the ESP tile-coordinate/monitor machinery consumes SOC_ROWS/SOC_COLS and
 * MONITOR_BASE_ADDR, and none of it is exercised on this platform.
 */
#ifndef __SOC_DEFS_H__
#define __SOC_DEFS_H__

#define SOC_ROWS 1
#define SOC_COLS 1
#define SOC_NCPU 1
#define SOC_NMEM 1
#define SOC_NDDR_CONTIG 1
#define ACCS_PRESENT 1
#define SOC_NACC 1

#define MONITOR_BASE_ADDR 0x60090000
#define MONITOR_TILE_SIZE 0x200

#define YX_WIDTH 4

#endif /* __SOC_DEFS_H__ */
