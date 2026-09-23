/*
 * ABI between QEMU's hw/misc/esp_vortex.c device model and the Verilated
 * GT_VORTEX_wrapper RTL bridge. Keep this header and the QEMU device in sync.
 */

#ifndef ESP_VORTEX_BRIDGE_H
#define ESP_VORTEX_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Guest-physical memory access callbacks supplied by QEMU. Both return 0 on
 * success and non-zero on failure.
 */
typedef int (*esp_vortex_mem_read_fn)(void *opaque, uint64_t gpa,
                                      void *data, size_t len);
typedef int (*esp_vortex_mem_write_fn)(void *opaque, uint64_t gpa,
                                       const void *data, size_t len);

void *esp_vortex_bridge_create(esp_vortex_mem_read_fn read_cb,
                               esp_vortex_mem_write_fn write_cb,
                               void *opaque);
void esp_vortex_bridge_destroy(void *bridge);
void esp_vortex_bridge_reset(void *bridge);
uint32_t esp_vortex_bridge_read(void *bridge, uint32_t offset);
void esp_vortex_bridge_write(void *bridge, uint32_t offset, uint32_t value);
int esp_vortex_bridge_run(void *bridge);

#ifdef __cplusplus
}
#endif

#endif /* ESP_VORTEX_BRIDGE_H */
