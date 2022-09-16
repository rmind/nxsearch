#
# This file is in the Public Domain.
#

BIN=		nxsearch
LIB=		libnxsearch

SYSNAME:=	$(shell uname -s)

ifeq ($(DEBUG),1)
DEBUG=		1
endif
ifeq ($(MAKECMDGOALS),tests)
DEBUG=		1
endif
ifeq ($(MAKECMDGOALS),debug)
DEBUG=		1
endif

#
# Common C compiler flags.
#
CFLAGS+=	-std=c2x -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-D_POSIX_C_SOURCE=200809L

CFLAGS+=	-I core/ -I index/ -I utils/

#
# Extended warning flags.
#
CFLAGS+=	-Wno-unknown-warning-option # gcc vs clang
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes -Wpointer-arith
CFLAGS+=	-Wmissing-declarations -Wredundant-decls -Wnested-externs
CFLAGS+=	-Wshadow -Wcast-qual -Wcast-align -Wwrite-strings
CFLAGS+=	-Wold-style-definition
CFLAGS+=	-Wsuggest-attribute=noreturn -Wjump-misses-init
CFLAGS+=	-Wduplicated-cond -Wmisleading-indentation -Wnull-dereference
CFLAGS+=	-Wduplicated-branches -Wrestrict

#
# Compiler-specific flags.
#
ifeq ($(CC),gcc)
CFLAGS+=	-Wa,--fatal-warnings -Wl,--fatal-warnings
endif
ifeq ($(CC),clang)
CFLAGS+=	-flto
LDFLAGS+=	-flto
endif

#
# Standard vs debug build flags.
#
ifdef DEBUG
CFLAGS+=	-O0 -ggdb -fno-omit-frame-pointer -DDEBUG
CFLAGS+=	-fsanitize=address -fsanitize=undefined
LDFLAGS+=	-fsanitize=address -fsanitize=undefined
else
CFLAGS+=	-DNDEBUG
endif

#
# System-specific compiler flags.
#

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE
LDFLAGS+=	-ljemalloc
endif

#
# Dependencies: compiler flags and libraries to link.
#

LDFLAGS+=	-lm

# ICU library
LDFLAGS+=	$(shell pkg-config --libs --cflags icu-uc icu-io)

# Snowball stemmer
LDFLAGS+=	-lstemmer

#
# objs
#

OBJS+=		core/nxs.o
OBJS+=		core/tokenizer.o
OBJS+=		core/filters.o
OBJS+=		core/filters_builtin.o
OBJS+=		core/ranking.o
OBJS+=		core/levdist.o

OBJS+=		index/idxmap.o
OBJS+=		index/idxterm.o
OBJS+=		index/idxdoc.o
OBJS+=		index/terms.o
OBJS+=		index/dtmap.o

OBJS+=		utils/strbuf.o
OBJS+=		utils/mmrw.o
OBJS+=		utils/utf8.o
OBJS+=		utils/log.o

#
# libs
#

HAVE_LIBS:=	$(shell test -d libs/rhashmap/src && echo 1 || echo 0)
ifeq ($(HAVE_LIBS),0)
define ERRMSG
ERROR: src/libs/* source code is missing.
Hint: probably did not initialize the Git submodules; consider running:

git submodule update --init --recursive


endef
$(error $(ERRMSG))
endif

EXT_OBJS+=	libs/rhashmap/src/rhashmap.o
EXT_OBJS+=	libs/rhashmap/src/siphash.o
EXT_OBJS+=	libs/rhashmap/src/murmurhash.o
CFLAGS+=	-I libs/rhashmap/src/

EXT_OBJS+=	libs/CRoaring/build/src/libroaring.a
CFLAGS+=	-I libs/CRoaring/include/

#
# object files for the tests
#
TEST_OBJS:=	$(shell echo $(OBJS) $(EXT_OBJS))
TEST_OBJS+=	tests/helpers.o
TESTS:=		$(patsubst tests/%.c,%,$(wildcard tests/t_*.c))

#
# Targets
#

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR)
install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)
install:	IBINDIR=	$(DESTDIR)/$(BINDIR)
install:	IMAN1DIR=	$(DESTDIR)/$(MANDIR)/man1/

all: $(OBJS) $(EXT_OBJS) core/cli.o
	$(CC) $(CFLAGS) $^ -o $(BIN) $(LDFLAGS)

lib: $(LIB).la

%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c $<

$(LIB).la: $(shell echo $(OBJS) $(EXT_OBJS) | sed 's/\.o/\.lo/g')
	libtool --mode=link --tag CC $(CC) $(LDFLAGS) -o $@ $(notdir $^)

libs/CRoaring/build/src/libroaring.a:
	@ echo Building libroaring
	cd libs/CRoaring && mkdir -p build && cd build && cmake .. && cmake --build .

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIB).la)
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR) && install -c $(INCS) $(IINCDIR)

install-bin:
	mkdir -p $(IBINDIR) && install -c $(BIN) $(IBINDIR)

clean:
	libtool --mode=clean rm
	rm -rf .libs *.o *.lo *.la
	rm -f $(BIN) $(OBJS) $(TESTS)

distclean: clean
	rm -f $(EXT_OBJS)

#
# Tests
#

t_%: $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ tests/$@.c -o $@ $(LDFLAGS)

tests: $(TESTS)
	@ for T in $(TESTS); do echo ./$$T; ./$$T; done

#
# Debugging
#

debug: all tests

.PHONY: all debug tests clean
