#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
profile=${1:-production}
board=${2:-dvt}
[[ $# -le 2 ]] || { echo 'Too many arguments' >&2; exit 2; }
case "$profile:$board" in
    production:dvt|production:bb03|factory:dvt|factory:bb03|bench:dvt|bench:bb03) ;;
    *) echo 'Usage: build-wsl.sh [production|factory|bench] [dvt|bb03]' >&2; exit 2 ;;
esac
python_path=${ARTOO_PYTHON:-$PWD/.venv/bin/python}
[[ -x "$python_path" ]] || { echo 'Create the pinned build Python environment first.' >&2; exit 1; }
# Quoted source-local includes would shadow binary-tree generated headers.
# Remove only these known ignored Tup outputs; never clean arbitrary ignored files.
for output in src/resources-gen.cpp src/resources-gen.h src/version.cpp; do
    [[ -e "$output" ]] || continue
    git check-ignore -q -- "$output" || { echo "Refusing to remove non-ignored $output" >&2; exit 1; }
    rm -- "$output"
done
cmake --preset "$profile-$board" \
    -DARTOO_PYTHON="$python_path" \
    -DARTOO_HOST_CC="${ARTOO_HOST_CC:-/usr/bin/gcc}" \
    -DARTOO_TOOLCHAIN_BIN="${ARTOO_TOOLCHAIN_BIN:-/usr/bin}"
cmake --build --preset "$profile-$board" --parallel "${ARTOO_JOBS:-$(nproc)}"
