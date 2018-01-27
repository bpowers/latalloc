-include config.mk

ifeq ($(VERSION),)

all:
	@echo "run ./configure before running make."
	@exit 1

else

LIBNAME          = latalloc

PREFIX = /usr

ARCH             = x86_64

LIB_SRCS         = src/main.cc
LIB_OBJS         = $(addprefix build/,$(LIB_SRCS:.cc=.o))
LIB              = lib$(LIBNAME).so

ALL_OBJS         = $(LIB_OBJS)

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


build/%.o: %.cpp build $(CONFIG)
	@echo "  CXX   $@"
	$(CXX) $(CXXFLAGS) -MMD -o $@ -c $<

$(LIB): $(LIB_OBJS) $(CONFIG)
	@echo "  LD    $@"
	$(CXX) -shared $(LDFLAGS) -o $@ $(LIB_OBJS) $(LIBS)

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
