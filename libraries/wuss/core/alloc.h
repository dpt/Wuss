/* wuss/core/alloc.h -- wuss - allocation through the configured hooks */

#ifndef WUSS_CORE_ALLOC_H
#define WUSS_CORE_ALLOC_H

#include "utils/alloc.h"
#include "utils/array.h"

#include "types.h"


/* Allocation through a wuss_t's configured hooks (see struct wuss::alloc).
 * Every heap block a wuss_t owns -- windows, icons, icon-pointer arrays, menu
 * nodes -- goes through these so a caller can supply its own allocator.
 * Strings and growable arrays use DPTLib's alloc_strdup and
 * array_grow_alloc on &wuss->alloc. */
static inline void *wuss__malloc(const wuss_t *wuss, size_t size)
{
  return wuss->alloc.malloc(size);
}

static inline void *wuss__realloc(const wuss_t *wuss, void *ptr, size_t size)
{
  return wuss->alloc.realloc(ptr, size);
}

static inline void wuss__free(const wuss_t *wuss, void *ptr)
{
  wuss->alloc.free(ptr);
}

#endif /* WUSS_CORE_ALLOC_H */
