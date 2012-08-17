About
=====

This is a wrapper for mmap() to expand the address range of the `MAP_32BIT` flag from 31bits to a full 32bits (i.e. the low 4Gbytes).  This allows access the almost a full 4Gbytes of 32bit addresses.

The main use of this wrapper is to allow LuaJIT to use more then 1Gbyte of ram on Linux.  With this wrapper LuaJIT can use amost a full 4Gbytes of ram.

**WARNING** **WARNING**
**This is experimental code**
**WARNING** **WARNING**

How it works
============

This library intercepts calls to mmap,mmap64,mremp,munmap.  On the first intercepted call it tries to grab as much of the low 4Gbytes of the processes address space as it can.  For now [dlmalloc](ftp://gee.cs.oswego.edu/pub/misc/malloc.c) is hacked to work as a page allocator for pages in this reserved address space.

Compile
=======

	./make.sh

Usage
=====

	$ LD_PRELOAD=mmap_low32.so luajit-2

TODO
====

* Try to only allocate pages from the kernel when needed, instead of one large block.
* Need to handle hints for addresses in low 4Gbytes of ram.
* Use a custom page allocator instead of the hacked dlmalloc.
* Add tunable environment variable to control how much address space to try to reserv.
* Allow using the system's mmap() for 32bit pages before falling back to custom page management when the system's mmap() fails.
* Make thread-safe
* Valgrind support.
* Cleanup code.
* Use `MADV_HUGEPAGE` to enable use of [Linux's Transparent Huge Pages](http://lwn.net/Articles/423584/) support.

