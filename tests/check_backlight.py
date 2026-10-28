#!/usr/bin/env python3
"""Real LCD setter with register objects; no LCD/peripheral boot simulation."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='artoo-backlight-') as directory:
    for board in (2,3):
        exe = pathlib.Path(directory) / f'backlight-{board}'
        subprocess.run(['g++','-std=c++11','-DARTOO_HOST_TEST',f'-DBOARD={board}',
            '-funsigned-char','-O1','-g','-ffunction-sections','-fdata-sections',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer',
            '-fno-pie','-no-pie','-Wl,--gc-sections','-I',str(root),
            '-include',str(root / 'tests/sys_wait_stub.h'),str(root / 'tests/backlight.cpp'),
            str(root / 'src/ili9341parallel.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
