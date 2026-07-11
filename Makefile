# iDFU Makefile
#
# Targets:
#   make macos    - build `idfu` for macOS (IOKit + limd stack)
#   make libusb   - build `idfu` for Linux/other using libusb + OpenSSL
#   make payload  - regenerate the payload .bin files from .S (needs gobjcopy)
#   make clean
#   make install  - install to /usr/local/bin
#
# DFU entry (guide) uses libimobiledevice / libusbmuxd / libirecovery / libplist
# (palera1n-style). Libraries default to limd-build install prefix:
#
#   LIMD_PREFIX   default $(HOME)/.local
#   LIMD_BUILD    default ../appleTV/limd-build  (extra include search)
#   LIMD_STATIC=1 default: static-link libirecovery (+ try .a for others
#                 that do not pull LibreSSL); dynamic-link libimobiledevice
#                 (needs OpenSSL/LibreSSL ABI matching the limd build).
#   LIMD_STATIC=0 fully dynamic against LIMD_PREFIX/lib
#
# Full static of libimobiledevice requires the same SSL the limd tree was
# built with (often in-tree LibreSSL). If that is unavailable, the hybrid
# default still static-embeds recovery/DFU (libirecovery) which is the
# critical path for Recovery -> DFU.

CC ?= clang
PROG = idfu

CORE_SRCS = src/idfu.c src/usb.c src/checkm8.c src/img4.c src/lzfse.c \
            src/usb_watcher.c
GUIDE_SRCS = src/dfu_guide.c src/dfu_enter.c
SRCS = $(CORE_SRCS) $(GUIDE_SRCS)

PAYLOAD_BINS = \
	payload/payload_A9.bin \
	payload/payload_notA9.bin \
	payload/payload_notA9_armv7.bin \
	payload/payload_handle_checkm8_request.bin \
	payload/payload_handle_checkm8_request_armv7.bin

GEN_HEADERS = \
	src/payload_A9.h \
	src/payload_notA9.h \
	src/payload_notA9_armv7.h \
	src/payload_handle_checkm8_request.h \
	src/payload_handle_checkm8_request_armv7.h

BACKEND ?= macos
LIMD_PREFIX ?= $(HOME)/.local
LIMD_STATIC ?= 1
LIMD_BUILD ?= $(abspath ../appleTV/limd-build)

.PHONY: macos libusb payload clean install all

all: macos

ifeq ($(NOGEN),1)
$(GEN_HEADERS):
	@echo "NOGEN=1: using pre-supplied $@"
else
src/payload_%.h: payload/payload_%.bin
	cd payload && xxd -iC $(notdir $<) > ../$@
endif

macos: BACKEND = macos
macos: $(PROG).macos
libusb: BACKEND = libusb
libusb: $(PROG).libusb

ifeq ($(NOGEN),1)
GEN_CLEAN =
else
GEN_CLEAN = $(RM) $(GEN_HEADERS)
endif

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

LIB_IRECV   := $(call find_a,libirecovery-1.0,libirecovery)
LIB_IDEVICE := $(call find_a,libimobiledevice-1.0,libimobiledevice)
LIB_USBMUXD := $(call find_a,libusbmuxd-2.0,libusbmuxd)
LIB_GLUE    := $(call find_a,libimobiledevice-glue-1.0,libimobiledevice-glue)
LIB_PLIST   := $(call find_a,libplist-2.0,libplist)

# Hybrid static: always force-load libirecovery (no SSL). Other limd libs
# dynamic from LIMD_PREFIX so OpenSSL/LibreSSL comes from that dylib build.
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

$(PROG).macos: $(GEN_HEADERS) $(SRCS) src/usb.h src/checkm8.h src/img4.h src/dfu_guide.h src/dfu_enter.h src/device_descriptor.h src/lzfse.h
	@echo "LIMD_PREFIX=$(LIMD_PREFIX) LIMD_STATIC=$(LIMD_STATIC)"
	@echo "  LIB_IRECV=$(LIB_IRECV)"
	xcrun -sdk macosx clang -mmacosx-version-min=12.0 \
		-Wall -Wextra -Wno-newline-eof -Wno-strict-prototypes \
		$(LIMD_CFLAGS) \
		$(SRCS) -o $(PROG) \
		$(LIMD_LIBS) \
		$(MAC_FRAMEWORKS) -Os
	$(GEN_CLEAN)
	@echo "Built ./$(PROG)"
	@otool -L $(PROG) | sed -n '1,16p'

$(PROG).libusb: $(GEN_HEADERS) $(SRCS) src/usb.h src/checkm8.h src/img4.h src/dfu_guide.h src/dfu_enter.h src/device_descriptor.h src/lzfse.h
	$(CC) -DHAVE_LIBUSB -Wall -Wextra -Wno-newline-eof \
		$(LIMD_CFLAGS) \
		$(SRCS) -o $(PROG) \
		$(LIMD_LIBS) \
		-lusb-1.0 -lcrypto -Os
	$(GEN_CLEAN)

payload:
	as -arch arm64 payload/payload_A9.S -o payload/payload_A9.o
	gobjcopy -O binary -j .text payload/payload_A9.o payload/payload_A9.bin
	$(RM) payload/payload_A9.o
	as -arch arm64 payload/payload_notA9.S -o payload/payload_notA9.o
	gobjcopy -O binary -j .text payload/payload_notA9.o payload/payload_notA9.bin
	$(RM) payload/payload_notA9.o
	as -arch armv7 payload/payload_notA9_armv7.S -o payload/payload_notA9_armv7.o
	gobjcopy -O binary -j .text payload/payload_notA9_armv7.o payload/payload_notA9_armv7.bin
	$(RM) payload/payload_notA9_armv7.o
	as -arch arm64 payload/payload_handle_checkm8_request.S -o payload/payload_handle_checkm8_request.o
	gobjcopy -O binary -j .text payload/payload_handle_checkm8_request.o payload/payload_handle_checkm8_request.bin
	$(RM) payload/payload_handle_checkm8_request.o
	as -arch armv7 payload/payload_handle_checkm8_request_armv7.S -o payload/payload_handle_checkm8_request_armv7.o
	gobjcopy -O binary -j .text payload/payload_handle_checkm8_request_armv7.o payload/payload_handle_checkm8_request_armv7.bin
	$(RM) payload/payload_handle_checkm8_request_armv7.o

clean:
	$(RM) $(PROG) $(PROG).macos $(PROG).libusb
ifneq ($(NOGEN),1)
	$(RM) $(GEN_HEADERS)
endif

install: $(PROG).macos
	cp $(PROG) /usr/local/bin/
