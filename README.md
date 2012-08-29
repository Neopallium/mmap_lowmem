About
=====

This is a wrapper for mmap() to expand the address range of the `MAP_32BIT` flag from 31bits to a full 32bits (i.e. the low 4Gbytes).  This allows access the almost a full 4Gbytes of 32bit addresses.

The main use of this wrapper is to allow LuaJIT to use more then 1Gbyte of ram on Linux.  With this wrapper LuaJIT can use amost a full 4Gbytes of ram.

Compile
=======

	$ make

Usage
=====

Dynamically during run-time

	$ LD_PRELOAD="./libmmap_lowmem.so" luajit-2

Or link luajit-2 with `libmmap_lowmem.so`

Getting every last bit of the low 4Gbytes available
===================================================

Compiling the LuaJIT executable as a [position-independent executable](http://en.wikipedia.org/wiki/Position-independent_code) will move the program brk (used by malloc) to an address outside the low 4Gbytes.

Use this make command to build LuaJIT:

	$ make all LDFLAGS=" -pie " CFLAGS=" -fPIC "

TODO
====

* Handle `MREMAP_MAYMOVE` & `MREMAP_FIXED` flags for mremap wrapper.
* Valgrind support.
* Cleanup code.

