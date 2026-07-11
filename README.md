# iDFU

macOS/Linux CLI to walk an Apple device into **DFU** and to leave **Recovery/DFU**
toward **normal mode**. No checkm8, no image upload — mode transitions only.

DFU entry matches [palera1n](https://github.com/palera1n/palera1n) `dfuhelper`
timing: lockdownd `EnterRecovery`, then libirecovery
`auto-boot=true` + `saveenv` + `irecv_reboot` (soft reboot, ~2s black screen)
with a guided button hold.

## Commands

| Command | Description |
|---------|-------------|
| `idfu guide` | Interactive normal/Recovery → DFU walk-through |
| `idfu exit` | Recovery → normal (`irecovery -n` semantics). From pure BootROM DFU, USB-reset + force-restart instructions |
| `idfu version` | Print version |

`idfu normal` is an alias for `idfu exit`.

## Build

### Dependencies

Built against a [libimobiledevice](https://libimobiledevice.org) stack
(libimobiledevice, libusbmuxd, libirecovery, libplist, libimobiledevice-glue).

Default prefix: `~/.local` (e.g. from limd-build). Override with `LIMD_PREFIX`.

```sh
# macOS
make macos
# or: make

# Linux (libusb for PID probe)
make libusb
```

Link modes:

- `LIMD_STATIC=1` (default): force-load `libirecovery.a`, dynamic-link the rest
  of the limd stack from `LIMD_PREFIX/lib` (avoids LibreSSL/OpenSSL ABI issues
  when fully static-linking libimobiledevice).
- `LIMD_STATIC=0`: fully dynamic.

```sh
make LIMD_PREFIX=$HOME/.local LIMD_STATIC=1
```

On Linux, install and run `usbmuxd` for normal-mode devices:

```sh
sudo apt install usbmuxd libusb-1.0-0-dev
sudo usbmuxd -f   # if not already running
```

## Usage

```sh
# Walk into DFU (unlock + Trust if starting from normal mode)
./idfu guide

# Leave Recovery (or attempt exit from DFU) toward iOS
./idfu exit
```

### Recovery → DFU button timing (A11 / no Home)

1. Ready fingers on **Volume Down + Side**.
2. Soft reboot is issued (`irecv_reboot`); screen goes black in ~2s.
3. Hold both ~2s through reboot, then hold **Volume Down** alone ~10s.
4. Tool reports DFU when PID `0x1227` appears.

Devices with a Home button use **Home + Power**, then **Home** alone.

### Exit to normal

- **Recovery**: `setenv auto-boot true` → `saveenv` → `irecv_reboot` (same as
  `irecovery -n`).
- **BootROM DFU**: no NVRAM; the tool may USB-reset and print a force-restart
  sequence. If `auto-boot` was set true during `guide`, a force restart should
  boot iOS instead of Recovery. If you land in Recovery, run `idfu exit` again.

## Layout

```
iDFU/
├── Makefile
├── LICENSE              # Apache-2.0
├── README.md
├── ent.plist            # legacy macOS USB entitlements (optional)
└── src/
    ├── idfu.c           # CLI dispatch
    ├── dfu_guide.{c,h}  # interactive guide + exit UX
    ├── dfu_enter.{c,h}  # limd probe / EnterRecovery / irecv helpers
    └── usb_probe.{c,h}  # VID/PID presence (IOKit or libusb)
```

## Scope

- **In scope**: mode detection, DFU entry guide, exit-to-normal.
- **Out of scope**: checkm8, PongoOS, kernel patching, jailbreak.

## Credits

- [palera1n](https://github.com/palera1n/palera1n) — DFU helper flow and timing
- [libimobiledevice](https://libimobiledevice.org) / libirecovery — device APIs
