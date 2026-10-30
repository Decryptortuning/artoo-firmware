#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile

from mavlink_fixture import include_flags
MAVLINK_FLAGS = include_flags()

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-calibration-") as directory:
    exe = pathlib.Path(directory) / "calibration"
    subprocess.run([
        "g++", *MAVLINK_FLAGS, "-std=c++11", "-DARTOO_HOST_TEST", "-funsigned-char", "-O1", "-g",
        "-ffast-math", "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti",
        "-fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero",
        "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-Wl,--gc-sections", "-I", str(root), str(root / "tests/calibration.cpp"),
        str(root / "src/stickaxis.cpp"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
