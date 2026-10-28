#!/usr/bin/env python3
"""Hardware-free tests of production codecs against the actual Linux decoder."""
import argparse
import pathlib
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--sololink-root", type=pathlib.Path, required=True)
args = parser.parse_args()
root = pathlib.Path(__file__).resolve().parents[1]
peer = args.sololink_root / "flightcode" / "stm32"
with tempfile.TemporaryDirectory(prefix="artoo-transport-") as directory:
    exe = pathlib.Path(directory) / "transport"
    subprocess.run([
        "g++", "-std=c++11", "-O1", "-g", "-Wall", "-Wextra",
        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
        "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-I", str(root), "-I", str(peer), str(root / "tests/transport.cpp"),
        str(peer / "SLIP.cpp"), "-o", str(exe),
    ], check=True)
    subprocess.run([str(exe)], check=True)
