#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-reliability-") as directory:
    exe = pathlib.Path(directory) / "reliability"
    subprocess.run([
        "g++", "-std=c++11", "-DARTOO_HOST_TEST", "-funsigned-char", "-O1", "-g",
        "-ffast-math", "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti",
        "-fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero",
        "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-Wl,--gc-sections", "-I", str(root), str(root / "tests/reliability.cpp"),
        str(root / "src/button.cpp"), str(root / "src/stickaxis.cpp"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
