#!/bin/sh
set -eu
# Explicit immutable export only; never deletes or generates in the source tree.
exec python3 "$(dirname "$0")/../tools/export_mavlink.py" "$@"
