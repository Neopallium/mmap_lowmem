#
# mmap lowmem library.
#

CFLAGS= -fPIC -O2 -Wall
LDFLAGS= -shared
LIBS= -ldl

CC= gcc
INSTALL= install -p
RM= rm -f

PREFIX= /usr

DPREFIX= $(DESTDIR)$(PREFIX)

LIBDIR= $(DPREFIX)/lib

MMAP_LIB= libmmap_lowmem.so

MMAP_MT_LIB= libmmap_lowmem_mt.so

MMAP_SRC= wrap_mmap.c mmap_lowmem.c page_alloc.c
MMAP_HEADER= wrap_mmap.h mmap_lowmem.h page_alloc.h lcommon.h

all: $(MMAP_LIB) $(MMAP_MT_LIB)

$(MMAP_LIB): $(MMAP_SRC) $(MMAP_HEADER)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(MMAP_SRC) $(LIBS)

$(MMAP_MT_LIB): $(MMAP_SRC) $(MMAP_HEADER)
	$(CC) $(LDFLAGS) $(CFLAGS) -DSUPPORT_THREADS=1 -o $@ $(MMAP_SRC) $(LIBS) -pthread

clean:
	$(RM) $(MMAP_LIB) $(MMAP_MT_LIB)

install:
	$(INSTALL) $(MMAP_LIB) $(LIBDIR)/
	$(INSTALL) $(MMAP_MT_LIB) $(LIBDIR)/

.PHONY: all clean install

