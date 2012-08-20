/***************************************************************************
 * Copyright (C) 2012 by Robert G. Jakabosky <bobby@neoawareness.com>      *
 *                                                                         *
 ***************************************************************************/
#if !defined(__PAGE_ALLOC_H__)
#define __PAGE_ALLOC_H__

#include "lcommon.h"

typedef struct PageAlloc PageAlloc;

L_LIB_API PageAlloc *page_alloc_new(uint8_t *addr, size_t len);

L_LIB_API uint8_t *page_alloc_get_segment(PageAlloc *palloc, uint8_t *addr, size_t len);

L_LIB_API uint8_t *page_alloc_resize_segment(PageAlloc *palloc, uint8_t *addr, size_t len, size_t new_len);

L_LIB_API int page_alloc_release_segment(PageAlloc *palloc, uint8_t *addr, size_t len);

L_LIB_API void page_alloc_dump_stats(PageAlloc *palloc);

#endif /* __PAGE_ALLOC_H__ */
