#include "lv_mem_psram_custom.h"

#include "esp_heap_caps.h"

void *lv_mem_psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void lv_mem_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}

void *lv_mem_psram_realloc(void *ptr, size_t new_size)
{
    return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
