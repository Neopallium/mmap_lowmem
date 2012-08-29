/***************************************************************************
 * Copyright (C) 2012 by Robert G. Jakabosky <bobby@neoawareness.com>      *
 *                                                                         *
 ***************************************************************************/

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>

#include <stdarg.h>

#ifdef SUPPORT_THREADS
#include <pthread.h>

static pthread_mutex_t page_alloc_lock = PTHREAD_MUTEX_INITIALIZER;
#define PAGE_LOCK() pthread_mutex_lock(&(page_alloc_lock))
#define PAGE_UNLOCK() pthread_mutex_unlock(&(page_alloc_lock))
#else
#define PAGE_LOCK() do { } while(0)
#define PAGE_UNLOCK() do { } while(0)
#endif

#include "wrap_mmap.h"

#include "page_alloc.h"

#define KBYTE (size_t)1024
#define MBYTE (KBYTE * 1024)
#define GBYTE (MBYTE * 1024)

#define ENABLE_VERBOSE 1

#if (ENABLE_VERBOSE != 1)
#define printf(...)
#endif

#define LOW_4G (uint8_t *)(4 * GBYTE)

#define REGION_CHECK(addr) \
	(((uint8_t *)(addr) >= region_start) && ((uint8_t *)(addr) < LOW_4G))

static void *lowmem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static void *lowmem_mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset);
static void *lowmem_mremap2(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr);
static int lowmem_munmap(void *addr, size_t length);

static WrapMMAP lowmem_wrap_mmap = {
	lowmem_mmap,
	lowmem_mmap64,
	lowmem_mremap2,
	lowmem_munmap,
};

static long sys_pagesize = 4096;

/* managed region. */
static uint8_t *region_start = NULL;

static PageAlloc *palloc = NULL;

#define M_FLAGS (MAP_PRIVATE|MAP_ANONYMOUS)

#if ENABLE_VERBOSE
static void dump_stats() {
	if(palloc) {
		page_alloc_dump_stats(palloc);
		getc(stdin);
	}
}
#endif

WrapMMAP *init_lowmem_mmap() {
	uint8_t *start;

#if ENABLE_VERBOSE
	atexit(dump_stats);
#endif
	sys_pagesize = sysconf(_SC_PAGE_SIZE);

	/* find the end of the bss/brk segment (and make sure it is page-aligned). */
	region_start = ((uint8_t *)(((ptrdiff_t)sbrk(0)) & ~(sys_pagesize - 1)) + sys_pagesize);
	/* check if brk is inside the low 4Gbytes. */
	if(region_start > LOW_4G) {
		/* brk is outside the low 4Gbytes, start managed region at lowest posible address */
		region_start = (uint8_t *)sys_pagesize;
	}
	start = SYS_MMAP(region_start, sys_pagesize, PROT_NONE, M_FLAGS, -1, 0);
	if(start == MAP_FAILED || start > LOW_4G) {
		if(start != MAP_FAILED) {
			int rc = SYS_MUNMAP(start, sys_pagesize);
			if(rc != 0) {
				perror("munmap() failed:");
			}
		}
#if ENABLE_VERBOSE
		printf("--- mmap failed: rc=%p, start=%p, end=%p\n", start, region_start, LOW_4G);
#endif
		/* fall back to using normal MAP_32BIT behavior. */
		region_start = NULL;
		start = NULL;
		return NULL;
	}
	/* keep one guard page between region_start and end of bss/brk. */
	start += sys_pagesize;
	region_start = start;
	palloc = page_alloc_new(region_start, LOW_4G - region_start);
#if ENABLE_VERBOSE
	printf("--- got low-mem: len=0x%zx, start=%p, end=%p\n",
		(LOW_4G - region_start), region_start, LOW_4G);
	getc(stdin);
#endif
	return &(lowmem_wrap_mmap);
}

static void *mmap_lowmem(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	void *mem;
	PAGE_LOCK();
	mem = page_alloc_get_segment(palloc, addr, length);
	PAGE_UNLOCK();
	if(mem == NULL) return MAP_FAILED;
	flags = (flags & ~(MAP_32BIT));
	mem = SYS_MMAP64(mem, length, prot, flags, fd, offset);
	if(mem == MAP_FAILED) {
		perror("mmap_lowmem(): mprotect failed");
		return MAP_FAILED;
	}
	return mem;
}

static void *lowmem_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	/* check if 'addr' hint is in low 4Gb range. */
	if(((flags & MAP_32BIT) && !(flags & MAP_FIXED)) || REGION_CHECK(addr)) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	/* fallback to system mmap. */
	return SYS_MMAP(addr, length, prot, flags, fd, offset);
}

static void *lowmem_mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	/* check if 'addr' hint is in low 4Gb range. */
	if(((flags & MAP_32BIT) && !(flags & MAP_FIXED)) || REGION_CHECK(addr)) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	/* fallback to system mmap. */
	return SYS_MMAP64(addr, length, prot, flags, fd, offset);
}

static void *lowmem_mremap2(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr) {
	if(flags & MREMAP_FIXED) {
		if(REGION_CHECK(new_addr)) {
printf("------ FAIL mremap(%p, %zd, %zd, 0x%x, %p)\n", old_addr, old_size, new_size, flags, new_addr);
			/* TODO: handle */
			return MAP_FAILED;
		}
		return SYS_MREMAP2(old_addr, old_size, new_size, flags, new_addr);
	}
	if(REGION_CHECK(old_addr)) {
		uint8_t *mem;
		//printf("32BIT_mremap(%p, %zd, %zd, 0x%x)\n", old_addr, old_size, new_size, flags);
		PAGE_LOCK();
		mem = page_alloc_resize_segment(palloc, old_addr, old_size, new_size);
		PAGE_UNLOCK();
		if(mem != old_addr) {
			if(flags & MREMAP_MAYMOVE) {
printf("------ FAIL mremap(%p, %zd, %zd, 0x%x)\n", old_addr, old_size, new_size, flags);
				/* TODO: try to support MREMAP_MAYMOVE. */
			}
			return MAP_FAILED;
		}
		/* we can resize the memory region in-place. */
		return SYS_MREMAP2(old_addr, old_size, new_size, flags, NULL);
	}
	return SYS_MREMAP2(old_addr, old_size, new_size, flags, NULL);
}

static int lowmem_munmap(void *addr, size_t length) {
	/* check if 'addr' is in low 4Gb range. */
	if(REGION_CHECK(addr)) {
		//printf("32BIT_munmap(%p, %zd)\n", addr, length);
		PAGE_LOCK();
		int rc = page_alloc_release_segment(palloc, addr, length);
		PAGE_UNLOCK();
		if(rc != 0) {
			errno = EINVAL;
			return -1;
		}
		if(SYS_MUNMAP(addr, length) != 0) {
			perror("munmap(): system munmap failed");
			return -1;
		}
		return 0;
	}
	//printf("munmap(%p, %zd)\n", addr, length);
	return SYS_MUNMAP(addr, length);
}

