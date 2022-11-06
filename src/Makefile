#
# This file is in the Public Domain.
#

LIB=		libnxsearch
LUALIB=		nxsearch

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
CFLAGS+=	-std=gnu11 -O2 -g -Wall -Wextra -Werror
CFLAGS+=	-D_POSIX_C_SOURCE=200809L

CFLAGS+=	-I core/ -I index/ -I algo/ -I utils/

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
CFLAGS+=	-fvisibility=hidden
endif

#
# System-specific compiler flags.
#

ifeq ($(SYSNAME),Linux)
CFLAGS+=	-D_GNU_SOURCE -D_DEFAULT_SOURCE
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
# Lua
#

USE_LUA?=	1

ifeq ($(USE_LUA),1)
CFLAGS+=	-fPIC $(shell pkg-config --cflags luajit)
LDFLAGS+=	-fPIC $(shell pkg-config --cflags --libs luajit)
endif

LUA_OBJS+=	core/lua.o

#
# objs
#

OBJS+=		core/nxs.o
OBJS+=		core/tokenizer.o
OBJS+=		core/filters.o
OBJS+=		core/filters_builtin.o
OBJS+=		core/filters_lua.o
OBJS+=		core/params.o
OBJS+=		core/results.o

OBJS+=		query/search.o

OBJS+=		index/idxmap.o
OBJS+=		index/idxterm.o
OBJS+=		index/idxdoc.o
OBJS+=		index/terms.o
OBJS+=		index/dtmap.o

OBJS+=		algo/ranking.o
OBJS+=		algo/heap.o
OBJS+=		algo/deque.o
OBJS+=		algo/levdist.o
OBJS+=		algo/bktree.o

OBJS+=		utils/strbuf.o
OBJS+=		utils/mmrw.o
OBJS+=		utils/utf8.o
OBJS+=		utils/log.o
OBJS+=		utils/utils.o

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

OBJS+=		libs/rhashmap/src/rhashmap.o
OBJS+=		libs/rhashmap/src/siphash.o
OBJS+=		libs/rhashmap/src/murmurhash.o
CFLAGS+=	-I libs/rhashmap/src/

EXT_AOBJ+=	libs/CRoaring/build/src/libroaring.a
CFLAGS+=	-I libs/CRoaring/include/

EXT_AOBJ+=	libs/yyjson/build/libyyjson.a
CFLAGS+=	-I libs/yyjson/src/

#
# object files for the tests
#
TEST_OBJS+=	tests/helpers.o
TESTS:=		$(patsubst tests/%.c,%,$(wildcard tests/t_*.c))

#
# Targets
#

BENCHMARK_BIN=	nxsearch_test
ALL_OBJS:=	$(shell echo $(OBJS) $(EXT_AOBJ))

$(LIB).la:	LDFLAGS+=	-rpath $(LIBDIR)

install/%.la:	ILIBDIR=	$(DESTDIR)/$(LIBDIR)
install:	IBINDIR=	$(DESTDIR)/$(BINDIR)
install:	LUA_ILIBDIR=	$(DESTDIR)/$(LUA_LIBDIR)/

all: $(ALL_OBJS) utils/benchmark.o
	$(CC) $(CFLAGS) $^ -o $(BENCHMARK_BIN) $(LDFLAGS)

lib: $(LIB).la

lua-lib: $(LUALIB).so

%.lo: %.c
	libtool --mode=compile --tag CC $(CC) $(CFLAGS) -c $<

%.la: $(shell echo $(OBJS) | sed 's/\.o/\.lo/g')
	libtool --mode=link --tag CC $(CC) $(LDFLAGS) -o $@ $(notdir $^) $(EXT_AOBJ)

%.so: $(ALL_OBJS) $(LUA_OBJS)
	$(CC) -fPIC -shared -o $@ $^ $(LDFLAGS)

libs/CRoaring/build/src/libroaring.a:
	@ echo Building libroaring
	cd libs/CRoaring && mkdir -p build && cd build && \
	    cmake -DENABLE_ROARING_TESTS=OFF .. && cmake --build .

libs/yyjson/build/libyyjson.a:
	@ echo Building yyjson
	cd libs/yyjson && mkdir -p build && cd build && \
	    cmake -DCMAKE_C_FLAGS="-fPIC" -DYYJSON_BUILD_TESTS=OFF .. && \
	    VERBOSE=1 cmake --build .

install/%.la: %.la
	mkdir -p $(ILIBDIR)
	libtool --mode=install install -c $(notdir $@) $(ILIBDIR)/$(notdir $@)

install: $(addprefix install/,$(LIB).la)
	libtool --mode=finish $(LIBDIR)
	mkdir -p $(IINCDIR) && install -c $(INCS) $(IINCDIR)

clean:
	libtool --mode=clean rm
	rm -rf .libs *.o *.so *.lo *.la utils/benchmark.o
	rm -f $(BENCHMARK_BIN) $(OBJS) $(TEST_OBJS) $(TESTS)

distclean: clean
	rm -rf libs/CRoaring/build libs/yyjson/build $(ALL_OBJS)

#
# Tests
#

t_%: $(ALL_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ tests/$@.c -o $@ $(LDFLAGS)

tests: $(TESTS)
	@ set -e && for T in $(TESTS); do echo ./$$T; ./$$T; done

#
# Debugging
#

debug: all tests

.PHONY: all debug tests clean
