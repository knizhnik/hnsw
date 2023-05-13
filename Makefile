EXTENSION = hnsw
EXTVERSION = 0.1.0

MODULE_big = hnsw
DATA = $(wildcard *--*.sql)
OBJS = hnsw.o hnswalg.o

TESTS = $(wildcard test/sql/*.sql)
REGRESS = $(patsubst test/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test --load-extension=hnsw

# For auto-vectorization:
# - GCC (needs -ftree-vectorize OR -O3) - https://gcc.gnu.org/projects/tree-ssa/vectorization.html
PG_CFLAGS += -O3
PG_CPPFLAGS +=  -msse4.1 -O3 -march=native  -openmp -ftree-vectorize -ftree-vectorizer-verbose=0
PG_LDFLAGS += -lstdc++

all: $(EXTENSION)--$(EXTVERSION).sql

ifdef USE_PGXS
PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/hnsw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

dist:
	mkdir -p dist
	git archive --format zip --prefix=$(EXTENSION)-$(EXTVERSION)/ --output dist/$(EXTENSION)-$(EXTVERSION).zip master
