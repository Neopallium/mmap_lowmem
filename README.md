About
=====

This is a wrapper for mmap() to expand the address range of the `MAP_32BIT` flag from 31bits to a full 32bits (i.e. the low 4Gbytes).  This allows access the almost a full 4Gbytes of 32bit addresses.

**WARNING** **WARNING**
**This is very experimental code**
**WARNING** **WARNING**

Compile
=======

	./make.sh

Usage
=====

	$ LD_PRELOAD=mmap_low32.so luajit-2

