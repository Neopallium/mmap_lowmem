/***************************************************************************
 * Copyright (C) 2012 by Robert G. Jakabosky <bobby@neoawareness.com>      *
 *                                                                         *
 ***************************************************************************/

#if !defined(__WRAP_MMAP_H__)
#define __WRAP_MMAP_H__

typedef void *(*mmap_t)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
typedef void *(*mmap64_t)(void *addr, size_t length, int prot, int flags, int fd, off64_t offset);
typedef void *(*mremap_t)(void *old_addr, size_t old_size, size_t new_size, int flags, ...);
typedef void *(*mremap2_t)(void *old_addr, size_t old_size, size_t new_size, int flags, void *new_addr);
typedef int (*munmap_t)(void *addr, size_t length);

typedef struct {
	mmap_t mmap;
	mmap64_t mmap64;
	mremap2_t mremap2;
	munmap_t munmap;
} WrapMMAP;

extern WrapMMAP *wrap_mmap;

extern WrapMMAP system_mmap;

#define SYS_MMAP(addr, length, prot, flags, fd, offset) \
	system_mmap.mmap((addr), (length), (prot), (flags), (fd), (offset))

#define SYS_MMAP64(addr, length, prot, flags, fd, offset) \
	system_mmap.mmap64((addr), (length), (prot), (flags), (fd), (offset))

#define SYS_MREMAP2(old_addr, old_size, new_size, flags, new_addr) \
	system_mmap.mremap2((old_addr), (old_size), (new_size), (flags), new_addr)

#define SYS_MUNMAP(addr, length) \
	system_mmap.munmap((addr), (length))

#endif /* __WRAP_MMAP_H__ */
