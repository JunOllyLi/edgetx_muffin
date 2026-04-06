#pragma once

#include <stdlib.h>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

inline void *colorLcdAlloc(size_t size)
{
#if defined(ESP_PLATFORM)
  void *ptr = heap_caps_malloc_prefer(
      size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (ptr) {
    return ptr;
  }
#endif
  return malloc(size);
}

inline void *colorLcdAlignedAlloc(size_t alignment, size_t size)
{
#if defined(ESP_PLATFORM)
  void *ptr = heap_caps_aligned_alloc(
      alignment, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (ptr) {
    return ptr;
  }
#endif
  return malloc(size);
}

inline void *colorLcdRealloc(void *ptr, size_t size)
{
#if defined(ESP_PLATFORM)
  void *res = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (res) {
    return res;
  }
#endif
  return realloc(ptr, size);
}
