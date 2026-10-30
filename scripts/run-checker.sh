#!/usr/bin/env bash
set -euo pipefail
# Analysis consumes an existing reviewed build's compilation database. It never
# cleans or builds the tree and never connects to a debugger/target.
[[ $# -ge 1 ]] || { echo 'Usage: run-checker.sh BUILD_DIRECTORY [clang-tidy arguments]' >&2; exit 2; }
build=$(realpath -- "$1")
shift
[[ -f "$build/compile_commands.json" ]] || { echo 'Build compilation database first.' >&2; exit 1; }
exec run-clang-tidy -p "$build" "$@"
