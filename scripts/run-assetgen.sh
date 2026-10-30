#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
[[ $# -eq 1 ]] || { echo 'Usage: run-assetgen.sh NEW_OUTPUT_DIRECTORY' >&2; exit 2; }
# Refuse reuse so a diagnostic regeneration cannot overwrite qualified assets.
mkdir -- "$1"
out=$(realpath -- "$1")
exec "${ARTOO_PYTHON:-$PWD/.venv/bin/python}" tools/assetgen.py resources/assets.cfg "$out" --show-stats
