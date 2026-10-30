#!/usr/bin/env bash
set -euo pipefail
cat >&2 <<'MESSAGE'
The Tup reference build is retired after exact CMake BIN/HEX parity.
Current source requires the reviewed external MAVLink export and uses CMake.
For a historical comparison, use commit 919156610cb6426394f190872b397d06059f7931
in a separate checkout and its original build-tup-reference.sh.
The accepted comparison is preserved under:
/home/jresp/solo/.integration/artoo-modernization-20261030T031656Z/reference4
Use scripts/build-wsl.sh for current source. No files were changed.
MESSAGE
exit 2
