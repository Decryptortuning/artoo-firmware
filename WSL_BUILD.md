# Native WSL firmware build

Working branch: `codex/artoo-wsl`. `UPSTREAM.json` records the canonical OpenSolo
monorepo/component split and original owner patch. The original PORT/main refs
and recovered checkout remain preserved; no remote history was rewritten.

Ubuntu 24.04 prerequisites (qualified here with GCC 13.2, Tup 0.7.11):

```bash
sudo apt-get install --no-install-recommends tup gcc-arm-none-eabi libnewlib-arm-none-eabi gdb-multiarch python3-venv
python3 -m venv .venv
.venv/bin/pip install -r requirements-build.txt
./scripts/build-wsl.sh
```

The wrapper generates and executes Tup's standalone build script. It cleans only
known ignored outputs before regenerating, retains compiler diagnostics as errors,
and runs a host UBSan/MAVLink wire-CRC regression on deliberately unaligned storage.
Outputs are artoo.elf, artoo.map, artoo.hex and artoo.bin. `VERSION` plus the source
commit identifies development builds; these are not upstream releases.

Pillow font geometry preserves the former top-bearing-inclusive height. Modern
GCC fixes use aligned checksum temporaries and annotate intentional LCD decoder
fallthrough. No runtime feature was deliberately changed by those fixes.

Check flash usage below 0x3f800 bytes, initial SP 0x2000c000, Thumb reset vector,
static RAM/stack headroom and constructor sections before packaging. Newlib emits
libnosys syscall-stub linker warnings; they remain visible in build logs.

A successful software build does not establish joystick calibration, physical
LCD appearance, power behavior or board compatibility. This work does not replace
the binary in Yocto or enable MCU updates. Hardware programming is a separate step.

## Correctness profiles and local evidence

`scripts/build-wsl.sh` defaults to `production dvt`. Explicit profiles are
`production|factory` and `dvt|bb03`, selected through tracked `configs/*.config`.
The factory profile enables output/GPIO/self-test and synthetic event requests;
production rejects these requests and does not link their handlers. Raw stick
I/O and calibration diagnostics remain available in production. Factory images
are separate bench artifacts and are never automatically packaged or flashed.

The build emits GCC `.su` stack-usage files. These describe individual compiled
functions, not complete interrupt nesting or a hardware high-water mark. The
linker reserves a minimum 4 KiB below the initial stack pointer and bounds newlib
heap growth below it. Firmware still needs physical stack/timing acceptance;
no watchdog or reset policy is introduced by these changes.

Local suites: `tests/check_transport.py --sololink-root PATH`,
`tests/check_reliability.py`, `tests/check_concurrency.py`,
`tests/check_runtime.py`, `tests/check_graphics.py`, and
`tests/check_mavlink.py`. Graphics tests regenerate into a temporary directory
and verify every image/glyph's RGB565 checksum against independent RLE decoding.
They do not overwrite the qualified build outputs. A separate coordinated
Renode harness in the integration repository owns startup/constructor and
bounded foreground-task smoke checks against an immutable ELF; it does not
emulate the full controller or qualify physical peripherals.

## Tamp asset generation

Image and glyph descriptors retain their sizes. The generator selects ordinary
palette RLE, Tamp-compressed palette indices or Tamp-compressed RLE independently
for each asset, including envelope bytes in its size decision. Palette order,
font geometry, RGB565 colors and the transparency key retain their baseline
meaning. `--uncompressed` is a host reference-generation option; normal builds
produce packed assets.

The pinned portable Tamp C sources and MIT/Apache licenses are in `vendor/tamp`.
Only decoder/common code links into firmware. Asset generation checks the vendor
hash manifest and builds the host-only compressor in a temporary directory with
native GCC; it does not install or import the Tamp Python package. The qualified
GCC/Pillow versions remain unchanged. A native C compiler is now required for
asset generation as well as the existing native test suites.

The foreground decoder uses one shared static 256-byte dictionary and a 32-byte
output chunk, rejecting ISR and recursive entry. Each asset resets its dictionary.
Envelope lengths, W8/extended/no-custom-dictionary headers, decoded-byte limits,
palette indices, pixel quota and canonical tail padding are checked. A malformed
stream may draw a validated prefix before failing; there is no full framebuffer
or promise of atomic display replacement. Streams are bounded, not authenticated:
a bit change can still encode a different valid image.

`tests/check_graphics.py` regenerates the immutable pre-compression baseline in a
temporary directory and compares every output pixel through both the actual
mixed decoder and Gfx sink, including tinted glyphs. It exercises 1/7/32-byte
chunks, truncation, header/length corruption, expansion limits, ISR/reentry and
fuzz cases under sanitizers. Hardware display latency remains an acceptance gate.
