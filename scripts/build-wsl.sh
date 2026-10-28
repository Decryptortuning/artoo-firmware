#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
[[ -x .venv/bin/python ]] || { echo 'Create .venv and install requirements-build.txt first.' >&2; exit 1; }
export PATH="$PWD/.venv/bin:$PATH"
# Generated-script builds do not register outputs with Tup's FUSE database.
# Remove only known ignored outputs before regenerating the build graph.
for output in src/resources-gen.cpp src/resources-gen.h obj/*.o artoo.elf artoo.hex artoo.bin artoo.map; do
    [[ -e "$output" ]] || continue
    git check-ignore -q -- "$output" || { echo "Refusing to remove non-ignored $output" >&2; exit 1; }
    rm -f -- "$output"
done
tup generate build-once.sh
./build-once.sh
python3 tests/check_mavlink.py
