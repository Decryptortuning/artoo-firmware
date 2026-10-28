#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-runtime-") as directory:
    for factory in (0,1):
        exe = pathlib.Path(directory) / f"runtime-{factory}"
        subprocess.run([
            "g++", "-std=c++11", "-DARTOO_HOST_TEST", f"-DARTOO_FACTORY_TESTS={factory}",
            "-funsigned-char", "-O1", "-g", "-ffunction-sections", "-fdata-sections",
            "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-Wl,--gc-sections", "-I", str(root), str(root / "tests/runtime.cpp"),
            str(root / "src/stm32/hwtimer.cpp"), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
