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

#include "wrap_mmap.h"
#include "mmap_lowmem.h"

#ifdef SUPPORT_THREADS
#include <pthread.h>
#endif

static void *init_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
static void *init_mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset);
static void *init_mremap2(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr);
static int init_munmap(void *addr, size_t length);

static WrapMMAP init_wrap_mmap = {
	init_mmap,
	init_mmap64,
	init_mremap2,
	init_munmap,
};

WrapMMAP *wrap_mmap = &(init_wrap_mmap);

WrapMMAP system_mmap;

#define INIT wrap_mmap_init()

static mremap_t sys_mremap = NULL;
static void *sys_mremap2(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr) {
	if(flags & MREMAP_FIXED) {
		return sys_mremap(old_addr, old_size, new_size, flags, new_addr);
	}
	return sys_mremap(old_addr, old_size, new_size, flags);
}

void wrap_mmap_init() {
	static int is_initialized = 0;
	WrapMMAP *wrapper;
#ifdef SUPPORT_THREADS
	pthread_mutex_t init_lock = PTHREAD_MUTEX_INITIALIZER;
#define INIT_UNLOCK() pthread_mutex_unlock(&init_lock)

	pthread_mutex_lock(&init_lock);
#else
#define INIT_UNLOCK() do { } while(0)
#endif
	if(is_initialized == 1) {
		INIT_UNLOCK();
		return;
	}

	is_initialized = 1;

	/* get system mmap functions. */
	system_mmap.mmap = (mmap_t)dlsym(RTLD_NEXT, "mmap");
	system_mmap.mmap64 = (mmap_t)dlsym(RTLD_NEXT, "mmap64");
	sys_mremap = (mremap_t)dlsym(RTLD_NEXT, "mremap");
	system_mmap.mremap2 = sys_mremap2;
	system_mmap.munmap = (munmap_t)dlsym(RTLD_NEXT, "munmap");

	/* try to initialize lowmem mmap. */
	wrapper = init_lowmem_mmap();
	if(wrapper == NULL) {
		/* failed to initialize lowmem mmap, fallback to system mmap. */
		wrapper = &(system_mmap);
	}
	wrap_mmap = wrapper;
	INIT_UNLOCK();
}

static void *init_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	INIT;
	return wrap_mmap->mmap(addr, length, prot, flags, fd, offset);
}

static void *init_mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	INIT;
	return wrap_mmap->mmap64(addr, length, prot, flags, fd, offset);
}

static void *init_mremap2(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr) {
	INIT;
	return wrap_mmap->mremap2(old_addr, old_size, new_size, flags, new_addr);
}

static int init_munmap(void *addr, size_t length) {
	INIT;
	return wrap_mmap->munmap(addr, length);
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	return wrap_mmap->mmap(addr, length, prot, flags, fd, offset);
}

void *mmap64(void *addr, size_t length, int prot, int flags, int fd, off64_t offset) {
	return wrap_mmap->mmap64(addr, length, prot, flags, fd, offset);
}

void *mremap(void *old_addr, size_t old_size, size_t new_size, int flags, ...) {
	void *new_addr = NULL;
	va_list ap;
	if(flags & MREMAP_FIXED) {
		/* get new_addr parameter. */
		va_start(ap, flags);
		new_addr = va_arg(ap, void *);
		va_end(ap);
	}
	return wrap_mmap->mremap2(old_addr, old_size, new_size, flags, new_addr);
}

int munmap(void *addr, size_t length) {
	return wrap_mmap->munmap(addr, length);
}

