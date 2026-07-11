# iDFU Makefile
#
# Targets:
#   make macos    - build `idfu` for macOS (IOKit probe + limd stack)
#   make libusb   - build `idfu` for Linux/other (libusb probe + limd)
#   make clean
#   make install  - install to /usr/local/bin
#
# Scope: DFU entry guide + exit-to-normal only (no checkm8 / payload).
#
# Libraries default to limd-build install prefix:
#   LIMD_PREFIX   default $(HOME)/.local
#   LIMD_BUILD    default ../appleTV/limd-build  (extra include search)
#   LIMD_STATIC=1 default: force-load static libirecovery; dynamic other limd
#   LIMD_STATIC=0 fully dynamic against LIMD_PREFIX/lib

CC ?= clang
PROG = idfu

SRCS = src/idfu.c src/dfu_guide.c src/dfu_enter.c src/usb_probe.c

LIMD_PREFIX ?= $(HOME)/.local
LIMD_STATIC ?= 1
LIMD_BUILD ?= $(abspath ../appleTV/limd-build)

.PHONY: macos libusb clean install all

all: macos

LIMD_CFLAGS = -I$(LIMD_PREFIX)/include
ifneq ($(wildcard $(LIMD_BUILD)/libirecovery/include/libirecovery.h),)
LIMD_CFLAGS += -I$(LIMD_BUILD)/libirecovery/include
LIMD_CFLAGS += -I$(LIMD_BUILD)/libimobiledevice/include
LIMD_CFLAGS += -I$(LIMD_BUILD)/libusbmuxd/include
LIMD_CFLAGS += -I$(LIMD_BUILD)/libplist/include
LIMD_CFLAGS += -I$(LIMD_BUILD)/libimobiledevice-glue/include
endif

define find_a
$(firstword $(wildcard $(LIMD_PREFIX)/lib/$(1).a) $(wildcard $(LIMD_BUILD)/$(2)/src/.libs/$(1).a))
endef

LIB_IRECV := $(call find_a,libirecovery-1.0,libirecovery)

LIMD_LIBS_COMMON = -L$(LIMD_PREFIX)/lib \
	-limobiledevice-1.0 -lusbmuxd-2.0 -limobiledevice-glue-1.0 -lplist-2.0 \
	-Wl,-rpath,$(LIMD_PREFIX)/lib

ifeq ($(LIMD_STATIC),1)
ifneq ($(LIB_IRECV),)
LIMD_LIBS = -Wl,-force_load,$(LIB_IRECV) $(LIMD_LIBS_COMMON)
else
$(warning libirecovery.a not found; fully dynamic link)
LIMD_LIBS = $(LIMD_LIBS_COMMON) -lirecovery-1.0
endif
else
LIMD_LIBS = $(LIMD_LIBS_COMMON) -lirecovery-1.0
endif

MAC_FRAMEWORKS = -framework CoreFoundation -framework IOKit -framework SystemConfiguration

macos: $(PROG)

$(PROG): $(SRCS) src/dfu_guide.h src/dfu_enter.h src/usb_probe.h
	@echo "LIMD_PREFIX=$(LIMD_PREFIX) LIMD_STATIC=$(LIMD_STATIC)"
	@echo "  LIB_IRECV=$(LIB_IRECV)"
	xcrun -sdk macosx clang -mmacosx-version-min=12.0 \
		-Wall -Wextra -Wno-newline-eof -Wno-strict-prototypes \
		$(LIMD_CFLAGS) \
		$(SRCS) -o $(PROG) \
		$(LIMD_LIBS) \
		$(MAC_FRAMEWORKS) -Os
	@echo "Built ./$(PROG)"
	@otool -L $(PROG) | sed -n '1,16p'

libusb:
	$(CC) -DHAVE_LIBUSB -Wall -Wextra -Wno-newline-eof \
		$(LIMD_CFLAGS) \
		$(SRCS) -o $(PROG) \
		$(LIMD_LIBS) \
		-lusb-1.0 -Os
	@echo "Built ./$(PROG)"

clean:
	$(RM) $(PROG) $(PROG).macos $(PROG).libusb

install: $(PROG)
	cp $(PROG) /usr/local/bin/
