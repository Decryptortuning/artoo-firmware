#!/usr/bin/env python3
"""Local production-policy/register tests; no UART, GPIO device or flight sends."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-concurrency-") as directory:
    exe = pathlib.Path(directory) / "concurrency"
    subprocess.run([
        "g++", "-std=c++11", "-DARTOO_HOST_TEST", "-funsigned-char", "-O1", "-g",
        "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti", "-pthread",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
        "-fno-pie", "-no-pie", "-Wl,--gc-sections", "-I", str(root),
        str(root / "tests/concurrency.cpp"), str(root / "src/button.cpp"),
        str(root / "src/stm32/dma.cpp"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
