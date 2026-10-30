#!/usr/bin/env python3
"""Native register adapters exercise actual ADC and bounded startup policy."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-startup-") as directory:
    exe = pathlib.Path(directory) / "startup"
    subprocess.run(["g++", "-std=c++11", "-DARTOO_HOST_TEST", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-I", str(root),
                    str(root / "tests/startup.cpp"),str(root / "src/stm32/adc.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
