#!/bin/sh
#
CC=gcc
#CC=clang

CFLAGS=" -g -Wall -Wno-unused-variable"

#$CC -fPIC $CFLAGS -c -o mmap_low32.o mmap_low32.c
#$CC -shared -g -o mmap_low32.so mmap_low32.o -ldl
$CC -shared -fPIC $CFLAGS -o mmap_low32.so mmap_low32.c -ldl

