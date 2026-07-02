# iDFU

A macOS/Linux command-line tool for putting Apple devices into DFU mode and
exploiting the [checkm8](https://github.com/0x7ff/gaster) bootrom vulnerability
to reach **PWNED DFU**, after which unsigned images (e.g. PongoOS) can be
uploaded and booted.

The checkm8 exploit core (USB primitives, four exploit stages, ROP/payload
assembly, chip parameter table, AES/KBag decryption, img4 DER parsing) is
adapted directly from [gaster](https://github.com/0x7ff/gaster) by 0x7ff, which
is licensed under the Apache License 2.0. The vendored `payload/*.bin` files and
`src/lzfse.{c,h}` also originate from gaster. The `dfu_guide` interactive mode
and the `idfu` subcommand layer are new code.

## Features

| Command | Description |
|---------|-------------|
| `idfu guide` | Interactive text-only walk-through to enter DFU mode (polls USB for Recovery/DFU enumeration). |
| `idfu pwn` | Run checkm8 to reach PWNED DFU. |
| `idfu boot <image>` | PWN (if not already), then DFU-upload and boot a raw unsigned image such as `PongoOS.bin`. |
| `idfu reset` | Clear DFU state and reset the device. |
| `idfu decrypt <src> <dst>` | Decrypt an img4/im4p using the device GID0 AES key. |
| `idfu decrypt_kbag <kbag>` | Decrypt a KBAG string using the device GID0 AES key. |
| `idfu version` | Print version. |

Environment variables:

- `USB_TIMEOUT` — USB timeout in ms (default `5`).
- `USB_ABORT_TIMEOUT_MIN` — minimum USB abort timeout in ms (default `0`).

## Supported chips

The parameter table covers CPID `0x8950`, `0x8955`, `0x8947`, `0x8960`,
`0x7001`, `0x7000`, `0x7002`, `0x8003`, `0x8000`, `0x8001`, `0x8002`,
`0x8004`, `0x8010`, `0x8011`, `0x8015`, `0x8012` — i.e. A7–A11 (and the
S-series variants). A12+ devices do **not** have DFU-mode checkm8.

## Build

### macOS (IOKit)

```sh
make macos
```

Requires Xcode Command Line Tools. On macOS you must run with `root`
(or grant the entitlements in `ent.plist`):

```sh
sudo ./idfu pwn
```

To sign with the bundled entitlements:

```sh
codesign -s - --entitlements ent.plist --force idfu
```

### Linux / other (libusb)

```sh
make libusb
```

Requires `libusb-1.0` and `openssl`:

```sh
# Debian/Ubuntu
sudo apt install libusb-1.0-0-dev libssl-dev
# The resulting binary needs root for raw USB access, or a udev rule:
sudo ./idfu pwn
```

## Example: boot PongoOS

```sh
# 1. Walk into DFU mode interactively.
sudo ./idfu guide

# 2. Exploit checkm8 (or let `boot` do it automatically).
sudo ./idfu pwn

# 3. Upload and boot PongoOS.
sudo ./idfu boot PongoOS.bin
```

## Project layout

```
iDFU/
├── Makefile
├── ent.plist            # macOS entitlements
├── LICENSE              # Apache-2.0
├── README.md
├── payload/             # vendored gaster payload binaries
│   ├── payload_A9.bin
│   ├── payload_notA9.bin
│   ├── payload_notA9_armv7.bin
│   ├── payload_handle_checkm8_request.bin
│   └── payload_handle_checkm8_request_armv7.bin
└── src/
    ├── idfu.c           # main + subcommand dispatch
    ├── dfu_guide.{c,h}  # interactive DFU entry guide (new)
    ├── usb.{c,h}        # USB backend: IOKit (macOS) / libusb (Linux), from gaster
    ├── device_descriptor.h
    ├── checkm8.{c,h}    # checkm8 stages + parameter table, from gaster
    ├── img4.{c,h}       # img4/im4p DER parsing + GID0 AES decrypt, from gaster
    ├── lzfse.{c,h}      # vendored from gaster (LZSS decompression)
    └── payload_*.h      # generated at build time (xxd -iC), not committed
```

## Scope and limitations

- iDFU provides a **DFU entry guide**, **checkm8 exploit**, and **unsigned
  image upload/boot**. It does **not** include KPF, kernel patching, or a
  full jailbreak — it is a "PWNED DFU prelude" tool.
- PongoOS / any boot image must be supplied by the user via `idfu boot`.
- Actual exploitation requires a device in DFU mode; without a device only
  the `guide`/usage paths can be exercised.
- Run as `root`. The bundled entitlements grant the USB/IOKit access
  needed on macOS.

## Credits

- [gaster](https://github.com/0x7ff/gaster) by 0x7ff — the checkm8 PoC and
  payload binaries this tool adapts. Licensed Apache-2.0.
- [checkra1n](https://checkra.in) — its DFU flow design informed the
  interactive guide and overall workflow.

## License

Apache License 2.0. See [LICENSE](LICENSE).