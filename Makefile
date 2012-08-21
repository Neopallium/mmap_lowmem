#
# mmap lowmem library.
#

CFLAGS= -fPIC -O2 -Wall
LDFLAGS= -shared

CC= gcc
INSTALL= install -p
RM= rm -f

PREFIX= /usr

DPREFIX= $(DESTDIR)$(PREFIX)

LIBDIR= $(DPREFIX)/lib

MMAP_LIB= libmmap_lowmem.so

MMAP_SRC= mmap_lowmem.c page_alloc.c
MMAP_HEADER= page_alloc.h lcommon.h

all: $(MMAP_LIB)

$(MMAP_LIB): $(MMAP_SRC) $(MMAP_HEADER)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $(MMAP_SRC) -ldl

clean:
	$(RM) $(MMAP_LIB)

install:
	$(INSTALL) $(MMAP_LIB) $(LIBDIR)/

.PHONY: all clean install

