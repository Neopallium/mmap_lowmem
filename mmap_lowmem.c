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

#include "page_alloc.h"

#define KBYTE (size_t)1024
#define MBYTE (KBYTE * 1024)
#define GBYTE (MBYTE * 1024)

#define ENABLE_VERBOSE 0

#if (ENABLE_VERBOSE != 1)
#define printf(...)
#endif

#define LOW_4G (uint8_t *)(4 * GBYTE)

#define REGION_CHECK(addr) \
	(region_start != NULL && region_start <= (uint8_t *)(addr) && (uint8_t *)(addr) <= region_end)

static int is_initialized = 0;

typedef void *(*mmap_t)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
typedef void *(*mmap64_t)(void *addr, size_t length, int prot, int flags, int fd, off64_t offset);
typedef void *(*mremap_t)(void *old_addr, size_t old_size, size_t new_size, int flags, ...);
typedef int (*munmap_t)(void *addr, size_t length);

static mmap_t sys_mmap = NULL;
static mmap64_t sys_mmap64 = NULL;
static mremap_t sys_mremap = NULL;
static munmap_t sys_munmap = NULL;
static long sys_pagesize = 4096;

/* managed region. */
static uint8_t *region_start = NULL;
static uint8_t *region_end = NULL;

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

static void mmap_lowmem_init() {
	uint8_t *start;

	is_initialized = 1;
#if ENABLE_VERBOSE
	atexit(dump_stats);
#endif
	sys_mmap = (mmap_t)dlsym(RTLD_NEXT, "mmap");
	sys_mmap64 = (mmap_t)dlsym(RTLD_NEXT, "mmap64");
	sys_mremap = (mremap_t)dlsym(RTLD_NEXT, "mremap");
	sys_munmap = (munmap_t)dlsym(RTLD_NEXT, "munmap");
	sys_pagesize = sysconf(_SC_PAGE_SIZE);

	/* find the end of the bss/brk segment (and make sure it is page-aligned). */
	region_start = ((uint8_t *)(((ptrdiff_t)sbrk(0)) & ~(sys_pagesize - 1)) + sys_pagesize);
	/* check if brk is inside the low 4Gbytes. */
	if(region_start > LOW_4G) {
		/* brk is outside the low 4Gbytes, start managed region at lowest posible address */
		region_start = (uint8_t *)sys_pagesize;
	}
	start = sys_mmap(region_start, sys_pagesize, PROT_NONE, M_FLAGS, -1, 0);
	if(start == MAP_FAILED || start > LOW_4G) {
		if(start != MAP_FAILED) {
			int rc = sys_munmap(start, sys_pagesize);
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
		return;
	}
	/* keep one guard page between region_start and end of bss/brk. */
	start += sys_pagesize;
	region_start = start;
	region_end = LOW_4G;
	palloc = page_alloc_new(region_start, region_end - region_start);
#if ENABLE_VERBOSE
	printf("--- got low-mem: len=0x%zx, start=%p, end=%p\n",
		(region_end - region_start), region_start, region_end);
	getc(stdin);
#endif
}
#define INIT if(is_initialized == 0) mmap_lowmem_init()

void *mmap_lowmem(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	void *mem;
	mem = page_alloc_get_segment(palloc, addr, length);
	if(mem == NULL) return MAP_FAILED;
	flags = (flags & ~(MAP_32BIT));
	mem = sys_mmap64(mem, length, prot, flags, fd, offset);
	if(mem == MAP_FAILED) {
		perror("mmap_lowmem(): mprotect failed");
		return MAP_FAILED;
	}
	return mem;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	INIT;
	if(region_start != NULL && flags & MAP_32BIT && !(flags & MAP_FIXED) && fd < 0) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	/* check if 'addr' hint is in low 4Gb range. */
	if(REGION_CHECK(addr)) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	return sys_mmap(addr, length, prot, flags, fd, offset);
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	INIT;
	if(region_start != NULL && flags & MAP_32BIT && !(flags & MAP_FIXED) && fd < 0) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	/* check if 'addr' hint is in low 4Gb range. */
	if(REGION_CHECK(addr)) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	return sys_mmap64(addr, length, prot, flags, fd, offset);
}

void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags, ...) {
	INIT;
	va_list ap;
	if(flags & MREMAP_FIXED) {
		void *new_addr;
		va_start(ap, flags);
		new_addr = va_arg(ap, void *);
		if(REGION_CHECK(new_addr)) {
printf("------ FAIL mremap(%p, %zd, %zd, 0x%x, %p)\n", old_addr, old_size, new_size, flags, new_addr);
			/* TODO: handle */
			return MAP_FAILED;
		}
		va_end(ap);
		return sys_mremap(old_addr, old_size, new_size, flags, new_addr);
	}
	if(REGION_CHECK(old_addr)) {
		uint8_t *mem;
		//printf("32BIT_mremap(%p, %zd, %zd, 0x%x)\n", old_addr, old_size, new_size, flags);
		mem = page_alloc_resize_segment(palloc, old_addr, old_size, new_size);
		if(mem != old_addr) {
			if(flags & MREMAP_MAYMOVE) {
printf("------ FAIL mremap(%p, %zd, %zd, 0x%x)\n", old_addr, old_size, new_size, flags);
				/* TODO: try to support MREMAP_MAYMOVE. */
			}
			return MAP_FAILED;
		}
		/* we can resize the memory region in-place. */
		return sys_mremap(old_addr, old_size, new_size, flags);
	}
	return sys_mremap(old_addr, old_size, new_size, flags);
}

int munmap(void *addr, size_t length) {
	/* check if 'addr' is in low 4Gb range. */
	if(REGION_CHECK(addr)) {
		//printf("32BIT_munmap(%p, %zd)\n", addr, length);
		int rc = page_alloc_release_segment(palloc, addr, length);
		if(rc != 0) {
			errno = EINVAL;
			return -1;
		}
		if(sys_munmap(addr, length) != 0) {
			perror("munmap(): sys_munmap failed");
			return -1;
		}
		return 0;
	}
	//printf("munmap(%p, %zd)\n", addr, length);
	return sys_munmap(addr, length);
}

