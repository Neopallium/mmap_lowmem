About
=====

This is a wrapper for mmap() to expand the address range of the `MAP_32BIT` flag from 31bits to a full 32bits (i.e. the low 4Gbytes).  This allows access the almost a full 4Gbytes of 32bit addresses.

The main use of this wrapper is to allow LuaJIT to use more then 1Gbyte of ram on Linux.  With this wrapper LuaJIT can use amost a full 4Gbytes of ram.

**WARNING**
**This is experimental code**
**WARNING**

Compile
=======

	./make.sh

Usage
=====

Dynamically during run-time

	$ LD_PRELOAD="./libmmap_lowmem.so" luajit-2

Or link luajit-2 with `libmmap_lowmem.so`

TODO
====

* Look for a way to change the start address of bss/program break to above the lower 4Gbytes of address space.
* Handle `MREMAP_MAYMOVE` & `MREMAP_FIXED` flags for mremap wrapper.
* Make thread-safe
* Valgrind support.
* Cleanup code.

