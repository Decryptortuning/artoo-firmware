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
