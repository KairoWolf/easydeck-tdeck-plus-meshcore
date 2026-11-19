// LVGL memory on the device: PSRAM first, internal RAM only as a fallback.
// With the C library allocator, every block under 4 KB (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL) -
// i.e. every label, button and style - landed in internal RAM, which Bluetooth and Wi-Fi need.
// The display draw buffers are allocated separately (internal, for DMA speed).
#include <lvgl.h>
#include <esp_heap_caps.h>

#define PSRAM_FIRST (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define FALLBACK    (MALLOC_CAP_DEFAULT)

extern "C" {

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void* lv_malloc_core(size_t size) { return heap_caps_malloc_prefer(size, 2, PSRAM_FIRST, FALLBACK); }
void* lv_realloc_core(void* p, size_t new_size) { return heap_caps_realloc_prefer(p, new_size, 2, PSRAM_FIRST, FALLBACK); }
void lv_free_core(void* p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t* mon_p) { (void)mon_p; }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

}
