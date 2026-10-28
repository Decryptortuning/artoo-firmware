#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='artoo-movingavg-') as directory:
    exe=pathlib.Path(directory)/'movingavg'
    subprocess.run(['g++','-std=c++11','-O1','-g','-ffast-math',
        '-fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero',
        '-fno-sanitize-recover=all','-fno-omit-frame-pointer','-fno-pie','-no-pie',
        '-I',str(root),str(root/'tests/movingavg.cpp'),str(root/'src/movingavgfilter.cpp'),
        '-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
