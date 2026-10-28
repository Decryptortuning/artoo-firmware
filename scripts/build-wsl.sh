#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
[[ -x .venv/bin/python ]] || { echo 'Create .venv and install requirements-build.txt first.' >&2; exit 1; }
export PATH="$PWD/.venv/bin:$PATH"
tup generate build-once.sh
./build-once.sh
python3 tests/check_mavlink.py
