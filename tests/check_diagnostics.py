#!/usr/bin/env python3
"""Native production record/host-contract tests; no device access."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-diagnostics-") as directory:
    exe = pathlib.Path(directory) / "diagnostics"
    subprocess.run(["g++", "-std=c++11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-I", str(root),
                    str(root / "tests/diagnostics.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
