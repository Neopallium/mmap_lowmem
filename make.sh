#!/bin/sh
#
CC=gcc
#CC=clang

CFLAGS=" -g -Wall -Wno-unused-variable"

#$CC -fPIC $CFLAGS -c -o mmap_lowmem.o mmap_lowmem.c
#$CC -shared -g -o libmmap_lowmem.so mmap_lowmem.o -ldl
$CC -shared -fPIC $CFLAGS -o libmmap_lowmem.so mmap_lowmem.c page_alloc.c -ldl

