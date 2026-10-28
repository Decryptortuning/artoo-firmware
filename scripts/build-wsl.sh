#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
[[ -x .venv/bin/python ]] || { echo 'Create .venv and install requirements-build.txt first.' >&2; exit 1; }
export PATH="$PWD/.venv/bin:$PATH"
profile=${1:-production}
board=${2:-dvt}
[[ $# -le 2 ]] || { echo 'Too many build arguments' >&2; exit 2; }
case "$profile:$board" in
    production:dvt|production:bb03|factory:dvt|factory:bb03|bench:dvt|bench:bb03) ;;
    *) echo 'Usage: build-wsl.sh [production|factory|bench] [dvt|bb03]' >&2; exit 2 ;;
esac
# Generated-script builds do not register outputs with Tup's FUSE database.
# Remove only known ignored outputs before regenerating the build graph.
for output in src/resources-gen.cpp src/resources-gen.h obj/*.o obj/*.su artoo.elf artoo.hex artoo.bin artoo.map build-manifest.json; do
    [[ -e "$output" ]] || continue
    git check-ignore -q -- "$output" || { echo "Refusing to remove non-ignored $output" >&2; exit 1; }
    rm -f -- "$output"
done
tup generate --config "configs/$profile-$board.config" build-once.sh
./build-once.sh
python3 tests/check_mavlink.py
# Bind the selected profile and exact embedded version to these outputs.
python3 - "$profile" "$board" <<'PYMANIFEST'
import hashlib, json, pathlib, subprocess, sys
root = pathlib.Path.cwd()
profile, board = sys.argv[1:]
bench = profile == "bench"
args = ["./git_describe_tag"] + (["--bench-noidle"] if bench else [])
version = subprocess.check_output(args, text=True).strip()
binary = (root / "artoo.bin").read_bytes()
if version.encode("ascii") + b"\0" not in binary:
    raise SystemExit("Expected selected build version is absent from binary")
config = root / "configs" / (profile + "-" + board + ".config")
commands = [line for line in (root/"build-once.sh").read_text().splitlines()
            if "arm-none-eabi-g" in line and " -c " in line]
expected_flags = ["-DBOARD=" + ("3" if board == "dvt" else "2"),
                  "-DARTOO_FACTORY_TESTS=" + str(int(profile == "factory")),
                  "-DARTOO_BENCH_NO_IDLE=" + str(int(bench))]
if not commands or any(flag + " " not in line for line in commands for flag in expected_flags):
    raise SystemExit("Generated compiler flags disagree with selected profile")
record = {
    "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
    "source_dirty": bool(subprocess.check_output(["git", "diff", "HEAD", "--"], text=True)),
    "version": version, "profile": profile, "board": board,
    "board_id": 3 if board == "dvt" else 2,
    "factory_tests": profile == "factory", "bench_no_idle": bench,
    "compile_commands_verified": len(commands),
    "config_path": str(config.relative_to(root)),
    "config_sha256": hashlib.sha256(config.read_bytes()).hexdigest(),
    "artifacts": {name: {"bytes": (root/name).stat().st_size,
                          "sha256": hashlib.sha256((root/name).read_bytes()).hexdigest()}
                  for name in ("artoo.bin", "artoo.elf", "artoo.hex", "artoo.map")},
}
(root / "build-manifest.json").write_text(json.dumps(record, indent=2) + "\n")
PYMANIFEST
