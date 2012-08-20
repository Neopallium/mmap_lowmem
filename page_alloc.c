/***************************************************************************
 * Copyright (C) 2012 by Robert G. Jakabosky <bobby@neoawareness.com>      *
 *                                                                         *
 ***************************************************************************/

#include "page_alloc.h"

#define ENABLE_STATS 1

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#if ENABLE_STATS
#include <stdio.h>
#endif

typedef struct Segment Segment;

#if 0
typedef uint32_t seg_t;
#define INVALID_SEG 0xFFFFFFFF
#else
typedef uint64_t seg_t;
#define INVALID_SEG 0xFFFFFFFFFF
#endif

struct PageAlloc {
	Segment   *seg;
	seg_t     seg_len;
	seg_t     free_list;   /* free memory list. */
	seg_t     unused_list; /* list of unused Segment structure. */
#if ENABLE_STATS
	seg_t     used_segs;
	seg_t     peak_used_segs;
#endif
};

struct Segment {
	seg_t   start;
	seg_t   len;
	seg_t   prev;
	seg_t   next;
};

#define INIT_SEGS 4

#define ADDR_TO_SEG(addr) (seg_t)((ptrdiff_t)(addr))
#define SEG_TO_ADDR(seg) (uint8_t *)((ptrdiff_t)(seg))

static void page_alloc_remove_seg(PageAlloc *palloc, seg_t id) {
	Segment *cur;
	Segment *prev;
	Segment *next;

	/* remove segment from free space list. */
	cur = palloc->seg + id;
	if(cur->prev != INVALID_SEG) {
		prev = palloc->seg + cur->prev;
		prev->next = cur->next;
	} else {
		/* removing the first segment in the free space list. */
		palloc->free_list = cur->next;
	}
	if(cur->next != INVALID_SEG) {
		next = palloc->seg + cur->next;
		next->prev = cur->prev;
	}

	/* add to head of unused segment list. */
	cur->start = INVALID_SEG;
	cur->len = 0;
	cur->prev = INVALID_SEG;
	cur->next = palloc->unused_list;
#if ENABLE_STATS
	palloc->used_segs--;
#endif
	palloc->unused_list = id;
}

static void page_alloc_grow_list(PageAlloc *palloc, seg_t new_len) {
	Segment *seg;
	seg_t old_len = palloc->seg_len;

	seg = (Segment *)realloc(palloc->seg, new_len * sizeof(Segment));
	palloc->seg = seg;
	palloc->seg_len = new_len;
	if(old_len < new_len) {
		seg_t i = new_len;
		seg_t next = palloc->unused_list;
		/* add new segments to list of unused segments. */
		do {
			i--;
			seg[i].start = INVALID_SEG;
			seg[i].len = 0;
			seg[i].prev = INVALID_SEG;
			seg[i].next = next;
			next = i;
		} while(i > old_len);
		palloc->unused_list = old_len;
	}
}

static seg_t page_alloc_get_unused_seg(PageAlloc *palloc) {
	seg_t id = palloc->unused_list;
	if(id == INVALID_SEG) {
		page_alloc_grow_list(palloc, palloc->seg_len + 100);
		id = palloc->unused_list;
	}
#if ENABLE_STATS
	palloc->used_segs++;
	if(palloc->used_segs > palloc->peak_used_segs) {
		palloc->peak_used_segs = palloc->used_segs;
	}
#endif
	palloc->unused_list = palloc->seg[id].next;
	return id;
}

static seg_t page_alloc_find_addr(PageAlloc *palloc, seg_t addr) {
	Segment *seg;
	seg_t prev;
	seg_t cur;

	prev = INVALID_SEG;
	cur = palloc->free_list;
	while(cur != INVALID_SEG) {
		seg = palloc->seg + cur;
		if(addr <= seg->start) {
			if(addr == seg->start) {
				/* found perfect match. */
				return cur;
			}
			/* address might be in range of previous segment. */
			return prev;
		}
		prev = cur;
		cur = seg->next;
	}
	return prev;
}

static seg_t page_alloc_free_space(PageAlloc *palloc, seg_t len) {
	Segment *seg;
	seg_t cur;

	/* first-fit search. */
	cur = palloc->free_list;
	while(cur != INVALID_SEG) {
		seg = palloc->seg + cur;
		if(len <= seg->len) break;
		cur = seg->next;
	}
	return cur;
}

static void page_alloc_add_free_seg(PageAlloc *palloc, seg_t addr, seg_t len) {
	seg_t id;
	Segment *seg;
	seg_t *prev_p;
	seg_t prev;
	seg_t cur;

	/* find first segment with higher start address. */
	prev = INVALID_SEG;
	prev_p = &(palloc->free_list);
	cur = palloc->free_list;
	while(cur != INVALID_SEG) {
		seg = palloc->seg + cur;
		if(addr < seg->start) break;
		prev = cur;
		prev_p = &(seg->next);
		cur = seg->next;
	}

	/* try to merge free space in to found segment. */
	if(cur != INVALID_SEG) {
		/* current segment is after free space. */
		if((addr + len) == seg->start) {
			/* pre-append free space on the start of segment. */
			seg->start = addr;
			seg->len += len;
			/* try merging with previous segment. */
			if(prev != INVALID_SEG) {
				Segment *prev_s = palloc->seg + prev;
				if(addr == (prev_s->start + prev_s->len)) {
					/* merge space into previous segment. */
					prev_s->len += seg->len;
					page_alloc_remove_seg(palloc, cur);
				}
			}
			return;
		}
		if(prev != INVALID_SEG) {
			/* try to merge free space into previous segment. */
			seg = palloc->seg + prev;
			if(addr == (seg->start + seg->len)) {
				/* append free space to end of the segment. */
				seg->len += len;
				/* we already know that the free space can't be merged with the next segment. */
				return;
			}
		}
		/* free space can't be merged with current/previous segments. */
	}

	/* setup a new segment */
	id = page_alloc_get_unused_seg(palloc);
	seg = palloc->seg + id;
	seg->start = addr;
	seg->len = len;
	/* insert segment into free space list. */
	*prev_p = id;
	seg->prev = prev;
	seg->next = cur;
	if(cur != INVALID_SEG) {
		palloc->seg[cur].prev = id;
	}

}

PageAlloc *page_alloc_new(uint8_t *addr, size_t len) {
	PageAlloc *palloc;

	palloc = (PageAlloc *)calloc(1, sizeof(PageAlloc));

	palloc->free_list = INVALID_SEG;
	palloc->unused_list = INVALID_SEG;
	palloc->seg_len = 0;
	palloc->seg = NULL;
	page_alloc_grow_list(palloc, INIT_SEGS);

	/* add free space. */
	page_alloc_add_free_seg(palloc, ADDR_TO_SEG(addr), len);

	return palloc;
}

static uint8_t *page_alloc_cut_segment(PageAlloc *palloc, seg_t id, uint8_t *addr, size_t len) {
	Segment *seg;
	seg_t start;

	seg = palloc->seg + id;
	start = ADDR_TO_SEG(addr);
	if(start != seg->start) {
		Segment *extra;
		seg_t extra_id;
		seg_t extra_len;
		seg_t prev;

		/* trim extra free space from start of segment. */
		extra_id = page_alloc_get_unused_seg(palloc);
		extra = palloc->seg + extra_id;
		extra_len = (start - seg->start);
		extra->start = seg->start;
		extra->len = extra_len;
		seg->start = start;
		seg->len -= extra_len;

		/* insert new segment. */
		prev = seg->prev;
		extra->prev = prev;
		extra->next = id;
		if(prev == INVALID_SEG) {
			palloc->free_list = extra_id;
		} else {
			palloc->seg[prev].next = extra_id;
		}
		seg->prev = extra_id;
	}

	/* trim requested space from start of segment. */
	seg->len -= len;
	if(seg->len == 0) {
		page_alloc_remove_seg(palloc, id);
	} else {
		seg->start += len;
	}
	return addr;
}

uint8_t *page_alloc_get_segment(PageAlloc *palloc, uint8_t *addr, size_t len) {
	Segment *seg;
	seg_t seg_end;
	seg_t start;
	seg_t id;

	if(addr != NULL) {
		start = ADDR_TO_SEG(addr);
		id = page_alloc_find_addr(palloc, start);
		if(id == INVALID_SEG) {
			/* failed to find a segment close to the address. */
			goto find_free_space;
		}
		seg = palloc->seg + id;
		/* make sure the requested range doesn't overlap a segment boundry. */
		seg_end = seg->start + seg->len;
		if((start + len) <= seg_end) {
			/* the requested address range is available. */
			return page_alloc_cut_segment(palloc, id, addr, len);
		}
		/* check if current segment is large enough for requested length. */
		if(len <= seg->len) {
			/* trim space from end of segment. */
			seg->len -= len;
			if(seg->len == 0) {
				addr = SEG_TO_ADDR(seg->start);
				page_alloc_remove_seg(palloc, id);
			} else {
				addr = SEG_TO_ADDR(seg->start + seg->len);
			}
			return addr;
		}
		/* can't allocate requested range. */
		/* ignore address hint and look for free space. */
	}
find_free_space:
	/* find the first segment that is large enough. */
	id = page_alloc_free_space(palloc, len);
	if(id == INVALID_SEG) return NULL;
	/* cut space from start of free space. */
	seg = palloc->seg + id;
	addr = SEG_TO_ADDR(seg->start);
	seg->start += len;
	seg->len -= len;
	if(seg->len == 0) {
		page_alloc_remove_seg(palloc, id);
	}
	return addr;
}

uint8_t *page_alloc_resize_segment(PageAlloc *palloc, uint8_t *addr, size_t len, size_t new_len) {
	seg_t end_addr = ADDR_TO_SEG(addr + len);
	Segment *seg;
	seg_t cur;
	if(new_len < len) {
		/* shrink allocated segment */
		page_alloc_add_free_seg(palloc, ADDR_TO_SEG(addr + new_len), len - new_len);
		return addr;
	}

	/* find next free segment. */
	cur = page_alloc_find_addr(palloc, end_addr);
	if(cur == INVALID_SEG) return NULL;
	seg = palloc->seg + cur;
	if(end_addr == seg->start) {
		seg_t need = new_len - len;
		if(need <= seg->len) {
			/* we can grow the allocated segment */
			seg->len -= need;
			if(seg->len == 0) {
				page_alloc_remove_seg(palloc, cur);
			} else {
				seg->start += need;
			}
			return addr;
		}
	}

	/* can't expand segment. */
	return NULL;
}

int page_alloc_release_segment(PageAlloc *palloc, uint8_t *addr, size_t len) {
	/* add free space. */
	page_alloc_add_free_seg(palloc, ADDR_TO_SEG(addr), len);
	return 0;
}

void page_alloc_dump_stats(PageAlloc *palloc) {
#if ENABLE_STATS
	printf("seg_len=%zd, used_segs=%zd, peak_used_segs=%zd\n",
		palloc->seg_len, palloc->used_segs, palloc->peak_used_segs);
#endif
}

