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

The current migration wrapper configures an isolated CMake/Ninja profile, retaining
compiler diagnostics as errors. Outputs are artoo.elf, artoo.map, artoo.hex and
artoo.bin under `build/PROFILE-BOARD`. Native regression suites are separate
qualification commands. The previous Tup build is preserved in Git at `9191566`
after exact parity; `scripts/build-tup-reference.sh` now explains that historical scope and exits. See the migration
section below. `VERSION` plus the source commit identifies development builds;
these are not upstream releases.

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
`production|factory|bench` and `dvt|bb03`, selected through tracked `configs/*.config`.
The factory profile enables output/GPIO/self-test and synthetic event requests;
production rejects these requests and does not link their handlers. Raw stick
I/O and calibration diagnostics remain available in production. Factory images
are separate bench artifacts and are never automatically packaged or flashed.

The explicit development command `./scripts/build-wsl.sh bench dvt` (or `bb03`)
starts with only the idle countdown disabled. It does not select updater UI,
pretend a vehicle is connected, enable factory handlers, change calibration, or
alter manual/battery shutdown logic. Normal production and factory builds keep
the original idle countdown and existing disable-button combo. There is no new
wire message or persistent setting. Return to a normal production image to
restore the default countdown on subsequent boots.

Bench images have the distinct `-bench-noidle` version suffix. Every wrapper
build emits ignored `build/PROFILE-BOARD/build-manifest.json` with the exact embedded version,
profile, board, source/config identity and artifact hashes. Copy this manifest
with immutable qualified artifacts and use its `version` for updater verification;
do not reconstruct a version from the checkout's current hash. The optional
`versioned-binaries.sh` packaging helper checks these hashes before naming files
from that manifest. Direct Tup builds do not emit the wrapper manifest.
`tests/check_idletimeout.py` exercises actual idle logic with inert external sinks
for default, explicit-normal and explicit-bench modes. It does not qualify
physical shutdown behavior.

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

## CMake migration candidate — October 29

The local modernization source adds CMake/Ninja as the intended maintained
build graph. This paragraph is an implementation description, not a parity-pass
claim. Qualification is recorded separately in the central modernization receipt.
The preserved prior entry point is `scripts/build-tup-reference.sh`; its historical
source-tree outputs must never run concurrently with the new graph. No compiler,
C++ standard, Pillow or Tamp upgrade is bundled with this migration.

The ordinary command remains `scripts/build-wsl.sh production dvt`. It now selects
one of six presets in `CMakePresets.json`, writing only to `build/PROFILE-BOARD`
after removing three known ignored source-tree Tup generation outputs. Direct
CMake configuration refuses those stale outputs, which otherwise shadow generated
headers through quoted includes. Add owned source files to `cmake/sources.cmake`;
configuration rejects unreviewed source-list drift. Factory and bench definitions,
C99/C++11, compiler options, GCC link driver and basename-sorted object order are
preserved for comparison. CMake does not automatically link libstdc++.

Host and target tools are distinct: `ARTOO_PYTHON` selects the pinned virtual
environment, `ARTOO_HOST_CC` selects native asset compression, and
`ARTOO_TOOLCHAIN_BIN` selects a complete matched Arm toolchain directory. Defaults
are `.venv/bin/python`, `/usr/bin/gcc` and `/usr/bin`. `ARTOO_JOBS` bounds parallel
compilation. Changing compiler directories requires a fresh profile build directory
(or a separately reviewed CMake configuration); do not mix compilers within old
object files. Native tests are explicit separate qualification commands, not
hidden in the build wrapper.

`build-inputs.json` records all tracked repository files and rejects untracked or
ignored files in compilation/generator/input directories. Only Python bytecode is
excluded there. `build-manifest.json` binds the exact profile/version/artifacts,
source identity, selected compiler programs and runtime libraries, multilib/specs,
Python/Pillow/FreeType installed bytes, native compiler, actual compile commands,
Ninja build commands and compiler header dependencies. Preparation and completion
reject source/tool changes during a build. Distro package inventory is supplemental
and does not assert that a privately selected compiler comes from those packages.
Installed package hashes do not replace wheel/archive hashes when fetching a new
dependency; retain those archives and hashes with the upgrade receipt.

Export using `scripts/versioned-binaries.sh build/production-dvt NEW_DIRECTORY`.
The destination must not exist and its parent must exist. All four artifacts are
validated against the manifest before claiming the destination, copied from the
validated bytes and checked after writing. A failed export retains an INCOMPLETE
marker; existing exports are never removed. Asset-only generation similarly takes
a new output directory. Static analysis consumes an existing compilation database
through `scripts/run-checker.sh BUILD_DIRECTORY`; it never cleans or rebuilds.

Before switching away from the preserved reference build, qualify all six profiles,
asset pixels, linker/vector/constructor and memory contracts, native regressions,
CMake incremental/source-change behavior and exact production/bench DVT Renode
binaries. Explain byte/ordering differences explicitly. A migration build is not
authorization to replace the Yocto-staged MCU payload or to flash hardware.

## Isolated dependency and MAVLink candidates

Freeze4 at `9191566` passed all six CMake builds, exact production-DVT Tup/CMake
BIN/HEX and asset-byte parity, unchanged incremental rebuild outputs and canonical
production/bench Renode. The receipt is
`/home/jresp/solo/.integration/artoo-modernization-20261030T031656Z`.
Tup is now historical inventory only; CMake is the sole maintained build graph.

Generated Solo headers now come from an explicit immutable export selected with
`ARTOO_MAVLINK_EXPORT`. Its `manifest.json` must contain `header_sha256`, mapping
every file under `include/` to its exact SHA256, and the entry point
`include/opensolo/mavlink/solo/mavlink.h`. The build verifies the entire inventory
before and after compilation and records the export manifest and header hashes.
The product-owned XML/generator export tool owns producing this directory; an
ambient sibling checkout is not an input. Changing exports changes dependencies.

Set `ARTOO_BUILD_ROOT` to a fresh absolute directory for compiler/Pillow comparisons;
the six profile subdirectories remain separate below it. Existing caches refuse
compiler-directory or Python-executable changes. `ARTOO_TOOLCHAIN_ARCHIVE` and
`ARTOO_PILLOW_WHEEL` select the preserved archives whose URLs, lengths and SHA256
are tracked in `dependency-pins.json`. Selected Arm15.3 and Pillow13 candidates
require those checked archives. Actual installed program/runtime/renderer hashes
remain independently recorded. Archive checksum validation is not a claim of
signature verification or proof that arbitrary installed tools were extracted
from those archives. No download or installation happens during a build.

For candidate graphics qualification run:

```bash
ARTOO_PYTHON=/absolute/candidate-venv/bin/python python3 tests/check_graphics.py
```

The candidate and repeat generations use that interpreter; the independent old
baseline always uses `.venv/bin/python` and requires Pillow10.2.0. This prevents a
Pillow upgrade from silently moving both sides of the raster comparison. Preserve
that baseline venv when promoting a separately qualified candidate. For the
unchanged baseline set ARTOO_PYTHON to the checkout's `.venv/bin/python` explicitly.
Default compiler/Pillow selection remains unchanged pending comparisons.

Native reliability, concurrency, calibration, idle and graphics fixtures include
the actual MCU host/flight headers. Set `ARTOO_MAVLINK_EXPORT` for those commands
as well; their shared fixture verifies the same contract/exporter/header hashes
as production. The dedicated MAVLink tests accept an explicit `--mavlink-export`.
