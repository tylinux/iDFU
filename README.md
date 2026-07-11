# iDFU

A macOS/Linux command-line tool for putting Apple devices into **DFU mode** and
exploiting the [checkm8](https://github.com/0x7ff/gaster) bootrom vulnerability
to reach **PWNED DFU**, after which unsigned images (e.g. PongoOS) can be
uploaded and booted.

## DFU entry (fixed)

`idfu guide` follows the same **non-exploit** orchestration as
[palera1n](https://github.com/palera1n/palera1n) `dfuhelper`:

| Step | Implementation |
|------|----------------|
| Detect mode | `libusbmuxd` (normal) + `libirecovery` (recovery/DFU) |
| Normal → Recovery | `lockdownd_enter_recovery` via **libimobiledevice** |
| Recovery prep | `irecv_setenv("auto-boot","true")` + `irecv_saveenv` |
| Soft reboot (~2s black screen) | `irecv_reboot` (**libirecovery**) |
| Button timing | Side + Volume Down (A11 / no Home) or Home + Power |
| Success | DFU PID `0x05AC:0x1227` |

This replaces the previous hand-rolled usbmux/plist/lockdown path and the
incorrect Recovery `DFU_CLR_STATUS` “auto reset” that did not match palera1n.

**Entering DFU is not a vulnerability exploit.** checkm8 runs only after DFU
via `idfu pwn` / `idfu boot`.

## Features

| Command | Description |
|---------|-------------|
| `idfu guide` | Normal/Recovery → DFU (limd + timed buttons). |
| `idfu pwn` | checkm8 → PWNED DFU. |
| `idfu boot <image>` | pwn if needed, upload + boot raw image (e.g. PongoOS). |
| `idfu reset` | Clear DFU state and reset the device. |
| `idfu decrypt <src> <dst>` | Decrypt img4/im4p with device GID0 AES key. |
| `idfu decrypt_kbag <kbag>` | Decrypt a KBAG string. |
| `idfu version` | Print version. |

Environment:

- `USB_TIMEOUT` — USB timeout in ms (default `5`).
- `USB_ABORT_TIMEOUT_MIN` — minimum USB abort timeout in ms (default `0`).

## Dependencies (limd stack)

Built against libraries from **limd-build** (or compatible install under
`~/.local`):

- libimobiledevice  
- libusbmuxd  
- libirecovery  
- libplist  
- libimobiledevice-glue  

Your tree: `/Users/tylinux/Developer/Projects/appleTV/limd-build`  
Typical install prefix: `~/.local` (`PREFIX` from `limd-build-macos.sh`).

### Link mode

| Variable | Meaning |
|----------|---------|
| `LIMD_PREFIX` | Header/lib root (default `$(HOME)/.local`) |
| `LIMD_BUILD` | Optional limd-build source tree for includes |
| `LIMD_STATIC=1` | **Default**: static-link `libirecovery.a` (recovery/DFU path); dynamic-link other limd dylibs from `LIMD_PREFIX` |
| `LIMD_STATIC=0` | Fully dynamic limd |

Full static `libimobiledevice` needs the **same SSL** the limd tree was built
with (often in-tree LibreSSL). Hybrid static irecovery is the practical default.

## Build

### macOS

```sh
# ensure limd is installed, e.g. limd-build-macos.sh → ~/.local
make macos
# or:
make macos LIMD_PREFIX=$HOME/.local LIMD_STATIC=1
```

Run (USB often needs root / entitlements):

```sh
sudo ./idfu guide
codesign -s - --entitlements ent.plist --force idfu
```

### Linux

```sh
make libusb
# needs libusb, openssl, and limd libs + usbmuxd
sudo ./idfu guide
```

## Example

```sh
# 1. Normal or Recovery → DFU
sudo ./idfu guide

# 2. checkm8
sudo ./idfu pwn

# 3. Boot PongoOS
sudo ./idfu boot PongoOS.bin
```

iPhone 8 / A11 (`CPID:8015`) button sequence after soft reboot:

1. Hold **Volume Down + Side** (~2s before and after reboot).  
2. Keep **Volume Down** through the black screen (~10s).  
3. Tool stops when DFU enumerates.

## Project layout

```
iDFU/
├── Makefile
├── ent.plist
├── LICENSE                 # Apache-2.0
├── README.md
├── payload/                # gaster payload binaries
└── src/
    ├── idfu.c              # CLI
    ├── dfu_enter.{c,h}     # limd: probe / EnterRecovery / autoboot / reboot
    ├── dfu_guide.{c,h}     # interactive guide (palera1n-style timing)
    ├── usb.{c,h}           # IOKit / libusb (gaster)
    ├── checkm8.{c,h}       # checkm8 (gaster)
    ├── img4.{c,h}          # img4 decrypt (gaster)
    ├── lzfse.{c,h}
    ├── usb_watcher.{c,h}
    └── plist/usbmux/lockdown  # legacy hand-rolled (unused by guide)
```

## Scope

- DFU entry + checkm8 + unsigned image upload.  
- No KPF / full jailbreak (use palera1n for that).  
- Supported checkm8 CPIDs: A7–A11 class (see gaster table). A12+ has no DFU checkm8.

## Credits

- [gaster](https://github.com/0x7ff/gaster) / 0x7ff — checkm8 core (Apache-2.0)  
- [palera1n](https://github.com/palera1n/palera1n) — DFU helper flow reference  
- [libimobiledevice](https://libimobiledevice.org) / libirecovery — device protocols  
