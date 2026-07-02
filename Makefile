# iDFU Makefile
#
# Targets:
#   make macos    - build `idfu` for macOS using IOKit/CoreFoundation
#   make libusb   - build `idfu` for Linux/other using libusb + OpenSSL
#   make payload  - regenerate the payload .bin files from .S (needs gobjcopy)
#   make clean
#   make install  - install to /usr/local/bin
#
# The 5 payload .bin files shipped in ./payload are vendored from gaster
# (0x7ff, Apache-2.0) and turned into C headers at build time via
# `xxd -iC`. The generated headers are removed after the build.

CC ?= clang
PROG = idfu
SRCS = src/idfu.c src/usb.c src/checkm8.c src/img4.c src/lzfse.c src/dfu_guide.c

PAYLOAD_BINS = \
	payload/payload_A9.bin \
	payload/payload_notA9.bin \
	payload/payload_notA9_armv7.bin \
	payload/payload_handle_checkm8_request.bin \
	payload/payload_handle_checkm8_request_armv7.bin

# xxd -iC generates `unsigned name_len` and `uint8_t name[]` arrays.
GEN_HEADERS = \
	src/payload_A9.h \
	src/payload_notA9.h \
	src/payload_notA9_armv7.h \
	src/payload_handle_checkm8_request.h \
	src/payload_handle_checkm8_request_armv7.h

BACKEND ?= macos

.PHONY: macos libusb payload clean install all

all: macos

# Generate a single C header from a payload .bin via `xxd -iC`.
# We cd into payload/ so xxd derives the symbol name from the bare
# filename (e.g. payload_A9_bin), matching the externs in checkm8.c.
# Set NOGEN=1 to skip header generation when they are pre-supplied
# (e.g. on hosts without xxd, after syncing the generated headers).
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

# Common build: generate headers, compile, then delete the generated headers
# unless NOGEN=1 (pre-supplied headers, kept in place).
ifeq ($(NOGEN),1)
GEN_CLEAN =
else
GEN_CLEAN = $(RM) $(GEN_HEADERS)
endif

# Common build: generate headers, compile, then delete the generated headers.
$(PROG).macos: $(GEN_HEADERS) $(SRCS) src/usb.h src/checkm8.h src/img4.h src/dfu_guide.h src/device_descriptor.h src/lzfse.h
	xcrun -sdk macosx clang -mmacosx-version-min=10.9 \
		-Wall -Wextra -Wpedantic \
		$(SRCS) -o $(PROG) \
		-framework CoreFoundation -framework IOKit -Os
	$(GEN_CLEAN)

$(PROG).libusb: $(GEN_HEADERS) $(SRCS) src/usb.h src/checkm8.h src/img4.h src/dfu_guide.h src/device_descriptor.h src/lzfse.h
	$(CC) -DHAVE_LIBUSB -Wall -Wextra -Wpedantic \
		$(SRCS) -o $(PROG) \
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