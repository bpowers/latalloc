-include config.mk

ifeq ($(VERSION),)

all:
	@echo "run ./configure before running make."
	@exit 1

else

LIBNAME          = latalloc

PREFIX = /usr

ARCH             = x86_64

LIB_SRCS         = src/liblatalloc.cc
LIB_OBJS         = $(addprefix build/,$(LIB_SRCS:.cc=.o))
LIB              = lib$(LIBNAME).so

ALL_OBJS         = $(LIB_OBJS)

HDR_HISTOGRAM    = src/vendor/HdrHistogram/CMakeLists.txt
HDR_BUILD_DIR    = build/src/vendor/HdrHistogram
HDR_BUILD        = $(HDR_BUILD_DIR)/Makefile
HDR_OBJS         = CMakeFiles/hdr_histogram.dir/hdr_encoding.c.o CMakeFiles/hdr_histogram.dir/hdr_histogram.c.o CMakeFiles/hdr_histogram.dir/hdr_histogram_log.c.o CMakeFiles/hdr_histogram.dir/hdr_interval_recorder.c.o CMakeFiles/hdr_histogram.dir/hdr_thread.c.o CMakeFiles/hdr_histogram.dir/hdr_time.c.o CMakeFiles/hdr_histogram.dir/hdr_writer_reader_phaser.c.o
HDR_LIB          = $(addprefix $(HDR_BUILD_DIR)/src/,$(HDR_OBJS))

# $(HDR_BUILD_DIR)/src/libhdr_histogram_static.a

ALL_SUBMODULES   = $(HDR_HISTOGRAM)

CONFIG           = Makefile config.mk

# quiet output, but allow us to look at what commands are being
# executed by passing 'V=1' to make, without requiring temporarily
# editing the Makefile.
ifneq ($V, 1)
MAKEFLAGS       += -s
endif

.SUFFIXES:
.SUFFIXES: .cc .cpp .S .c .o .d .test

all: $(LIB)

build:
	mkdir -p build
	mkdir -p $(basename $(ALL_OBJS))
	touch -c build

$(ALL_SUBMODULES):
	@echo "  GIT   $@"
	git submodule update --init
	touch -c $@

$(HDR_BUILD): $(HDR_HISTOGRAM) $(CONFIG)
	@echo "  CMAKE $@"
	mkdir -p $(HDR_BUILD_DIR)
	cd $(HDR_BUILD_DIR) && CC=$(CC) CXX=$(CXX) cmake $(realpath $(dir $(HDR_HISTOGRAM)))
	touch -c $(HDR_BUILD)

$(HDR_LIB): $(HDR_BUILD) $(CONFIG)
	@echo "  LD    $@"
	cd $(HDR_BUILD_DIR) && $(MAKE)
	touch -c $@

build/src/%.o: src/%.c build $(CONFIG)
	@echo "  CC    $@"
	$(CC) $(CFLAGS) -MMD -o $@ -c $<

build/%.o: %.cc build $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(CXXFLAGS) -MMD -o $@ -c $<

$(LIB): $(LIB_OBJS) $(HDR_LIB) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) -o $@ $(LIB_OBJS) $(LIBS) $(HDR_LIB)

install: $(LIB)
	install -c -m 0755 $(LIB) $(PREFIX)/lib/$(LIB)
	ldconfig

format:
	clang-format -i src/*.cc src/*.h

endif

clean:
	rm -f $(LIB)
	find . -name '*~' -print0 | xargs -0 rm -f
	rm -rf build


distclean: clean

# double $$ in egrep pattern is because we're embedding this shell command in a Makefile
TAGS:
	@echo "  TAGS"
	find . -type f | egrep '\.(cpp|h|cc|hh)$$' | grep -v google | xargs etags -l c++

-include $(ALL_OBJS:.o=.d)

.PHONY: all clean distclean format install paper run TAGS
