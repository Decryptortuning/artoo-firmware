#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
[[ $# -eq 2 ]] || { echo 'Usage: versioned-binaries.sh BUILD_DIRECTORY NEW_EXPORT_DIRECTORY' >&2; exit 2; }
exec "${ARTOO_PYTHON:-$PWD/.venv/bin/python}" tools/export_artifacts.py "$1" "$2"
