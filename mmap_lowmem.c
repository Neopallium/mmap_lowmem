/***************************************************************************
 * Copyright (C) 2012 by Robert G. Jakabosky <bobby@neoawareness.com>      *
 *                                                                         *
 ***************************************************************************/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <dlfcn.h>
#include <unistd.h>
#undef _GNU_SOURCE
#undef __USE_GNU
#include <sys/mman.h>

#define USE_DLMALLOC 1

#if USE_DLMALLOC
static void *mmap_lowmem_sbrk(ptrdiff_t incr);
#define MORECORE                  mmap_lowmem_sbrk
#define USE_DL_PREFIX
#define MSPACES                   0
#define HAVE_MORECORE             1
#define MORECORE_CONTIGUOUS       1
#define HAVE_MMAP                 0
#define HAVE_MREMAP               0
#define MMAP_CLEARS               0
#define DEFAULT_GRANULARITY       4096
#define malloc_getpagesize        4096
#define DEFAULT_MMAP_THRESHOLD    1
#define REALLOC_ZERO_BYTES_FREES

#include "dlmalloc.c"
#endif

#define KBYTE (size_t)1024
#define MBYTE (KBYTE * 1024)
#define GBYTE (MBYTE * 1024)

#define ENABLE_VERBOSE 1

#if (ENABLE_VERBOSE != 1)
#define printf(...)
#endif

#define REGION_MAX (uint8_t *)(4 * GBYTE)

//#define REGION_LENGTH (size_t)(2 * GBYTE)
//#define REGION_LENGTH (size_t)((4 * GBYTE) - (256 * MBYTE))
#define REGION_LENGTH (size_t)(REGION_MAX - region_start)
#define REGION_CHECK(addr) (region_start <= (uint8_t *)(addr) && (uint8_t *)(addr) <= region_end)

#define PAGE_ALIGN_CHECK(func, addr) do { \
	if(((ptrdiff_t)(addr)) & (sys_pagesize - 1)) { \
		printf("------- Badly aligned memory from " #func ": %p\n", (addr)); \
		abort(); \
	} \
} while(0)

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
static uint8_t *next_page = NULL;
static uint8_t *brk_ptr = NULL;
static uint8_t *region_start = NULL;
static uint8_t *region_end = NULL;

#define M_FLAGS (MAP_PRIVATE|MAP_ANONYMOUS)

void mmap_lowmem_init() {

	is_initialized = 1;
	sys_mmap = (mmap_t)dlsym(RTLD_NEXT, "mmap");
	sys_mmap64 = (mmap_t)dlsym(RTLD_NEXT, "mmap64");
	sys_mremap = (mremap_t)dlsym(RTLD_NEXT, "mremap");
	sys_munmap = (munmap_t)dlsym(RTLD_NEXT, "munmap");
	sys_pagesize = sysconf(_SC_PAGE_SIZE);

	/* find the end of the bss/brk segment. */
	region_start = sbrk(0);
	/* page align region_start. */
	region_start = ((uint8_t *)(((ptrdiff_t)region_start) & ~(sys_pagesize - 1)) + (2 * sys_pagesize));
	next_page = sys_mmap(region_start, REGION_LENGTH, PROT_NONE, M_FLAGS, -1, 0);
	if(next_page == MAP_FAILED || next_page > REGION_MAX) {
		if(next_page != MAP_FAILED) {
			int rc = sys_munmap(next_page, REGION_LENGTH);
			if(rc != 0) {
				perror("munmap() failed:");
			}
		}
		printf("--- mmap failed: rc=%p, start=%p, end=%p\n",
			next_page, region_start, region_start + REGION_LENGTH);
		region_start = NULL;
		next_page = NULL;
		return;
	}
	region_start = next_page;
	region_end = next_page + REGION_LENGTH;
	brk_ptr = next_page;
	printf("--- got low-mem: len=%zd, start=%p, end=%p\n", REGION_LENGTH, region_start, region_end);
#if ENABLE_VERBOSE
getc(stdin);
#endif
}
#define INIT if(is_initialized == 0) mmap_lowmem_init()

#if USE_DLMALLOC
static void *mmap_lowmem_sbrk(ptrdiff_t incr) {
	void *mem = brk_ptr;
	brk_ptr += incr;
	if(incr >= 0) {
		if(brk_ptr > region_end) {
			brk_ptr -= incr;
			errno = ENOMEM;
			return MAP_FAILED;
		}
		if(brk_ptr > next_page) {
			size_t len = brk_ptr - next_page;
			len = (len & ~(sys_pagesize - 1)) + sys_pagesize;
			/* need to extand accessible memory range. */
			if(mprotect(next_page, len, PROT_READ|PROT_WRITE)) {
				perror("mmap_lowmem_sbrk() mprotect failed");
			}
			next_page += len;
		}
	} else {
		/* try to trim pages. */
		uint8_t *old_next_page = next_page;
		size_t len;
		next_page = (uint8_t *)(((ptrdiff_t)brk_ptr) & ~(sys_pagesize - 1)) + sys_pagesize;
		len = old_next_page - next_page;
		/* release the pages. */
		if(sys_munmap(next_page, len)) {
			perror("mmap_lowmem_sbrk() munmap failed");
		}
		/* re-protect the pages. */
		if(sys_mmap(next_page, len, PROT_NONE, M_FLAGS, -1, 0) == MAP_FAILED) {
			perror("mmap_lowmem_sbrk() mprotect failed");
		}
	}
	return mem;
}
#endif

void *mmap_lowmem(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	void *mem;
#if !USE_DLMALLOC
	//printf("32BIT_mmap64(%p, %zd, 0x%x, 0x%x, %d, %zd)\n", addr, length, prot, flags, fd, offset);
	/* remove MAP_32BIT and add MAP_FIXED */
	flags = (flags & ~(MAP_32BIT)) | MAP_FIXED;
	mem = sys_mmap64(next_page, length, prot, flags, fd, offset);
//printf("32BIT_mmap64(%p, %zd, 0x%x, 0x%x, %d, %zd)\n", next_page, length, prot, flags, fd, offset);
	if(mem != MAP_FAILED) {
		/* bump next page. */
		next_page += length;
	}
#else
	mem = dlvalloc(length);
	if(mem == NULL) {
		return MAP_FAILED;
	}
	PAGE_ALIGN_CHECK(dlvalloc, mem);
#endif
	return mem;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	INIT;
	if(region_start != NULL && flags & MAP_32BIT && !(flags & MAP_FIXED) && fd < 0) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	return sys_mmap(addr, length, prot, flags, fd, offset);
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	INIT;
	if(region_start != NULL && flags & MAP_32BIT && !(flags & MAP_FIXED) && fd < 0) {
		return mmap_lowmem(addr, length, prot, flags, fd, offset);
	}
	return sys_mmap64(addr, length, prot, flags, fd, offset);
}

void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr) {
	INIT;
	if(REGION_CHECK(old_addr)) {
		uint8_t *mem;
		//printf("32BIT_mremap(%p, %zd, %zd, 0x%x, %p)\n", old_addr, old_size, new_size, flags, new_addr);
#if USE_DLMALLOC
		mem = dlrealloc_in_place(old_addr, new_size);
		if(mem == 0) {
			/* need to move memory. */
			mem = dlvalloc(new_size); /* allocate new. */
			if(mem == NULL) {
				return MAP_FAILED;
			}
			/* copy data old to new. */
			if(old_size < new_size) {
				memcpy(mem, old_addr, old_size);
			} else {
				memcpy(mem, old_addr, new_size);
			}
			/* free old. */
			dlfree(old_addr);
		}
		PAGE_ALIGN_CHECK(dlrealloc, mem);
		return mem;
#endif
	}
	return sys_mremap(old_addr, old_size, new_size, flags, new_addr);
}

int munmap(void *addr, size_t length) {
	/* check if */
	if(REGION_CHECK(addr)) {
		//printf("32BIT_munmap(%p, %zd)\n", addr, length);
#if USE_DLMALLOC
		dlfree(addr);
		return 0;
#endif
	}
	//printf("munmap(%p, %zd)\n", addr, length);
	return sys_munmap(addr, length);
}

