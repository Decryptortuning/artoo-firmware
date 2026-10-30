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
build_root=${ARTOO_BUILD_ROOT:-$PWD/build}
build_directory="$build_root/$profile-$board"
# Refuse switching tools inside an existing profile cache. Select a new root for
# compiler/Pillow comparisons instead of reusing artifacts across candidates.
"$python_path" - "$build_directory/CMakeCache.txt" "$python_path" "${ARTOO_TOOLCHAIN_BIN:-/usr/bin}" <<'PY'
import pathlib,sys
p=pathlib.Path(sys.argv[1])
if p.exists():
    values=dict(line.split('=',1) for line in p.read_text().splitlines() if '=' in line and not line.startswith(('#','//')))
    for key,wanted in [('ARTOO_PYTHON:FILEPATH',sys.argv[2]),('ARTOO_TOOLCHAIN_BIN:PATH',sys.argv[3])]:
        if key in values and pathlib.Path(values[key]).absolute()!=pathlib.Path(wanted).absolute():
            raise SystemExit('Tool selection changed in existing build; choose a new ARTOO_BUILD_ROOT')
PY
# Quoted source-local includes would shadow binary-tree generated headers.
# Remove only these known ignored Tup outputs; never clean arbitrary ignored files.
for output in src/resources-gen.cpp src/resources-gen.h src/version.cpp; do
    [[ -e "$output" ]] || continue
    git check-ignore -q -- "$output" || { echo "Refusing to remove non-ignored $output" >&2; exit 1; }
    rm -- "$output"
done
cmake --preset "$profile-$board" -B "$build_directory" \
    -DARTOO_PYTHON="$python_path" \
    -DARTOO_HOST_CC="${ARTOO_HOST_CC:-/usr/bin/gcc}" \
    -DARTOO_TOOLCHAIN_BIN="${ARTOO_TOOLCHAIN_BIN:-/usr/bin}" \
    -DARTOO_MAVLINK_EXPORT="${ARTOO_MAVLINK_EXPORT:-}" \
    -DARTOO_TOOLCHAIN_ARCHIVE="${ARTOO_TOOLCHAIN_ARCHIVE:-}" \
    -DARTOO_PILLOW_WHEEL="${ARTOO_PILLOW_WHEEL:-}"
cmake --build "$build_directory" --parallel "${ARTOO_JOBS:-$(nproc)}"
