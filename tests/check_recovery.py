#!/usr/bin/env python3
"""Actual journal/policy templates, power-loss injection; no hardware."""
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='artoo-recovery-') as tmp:
    exe=pathlib.Path(tmp)/'recovery'
    subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie','-I',str(root),str(root/'tests/recovery.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
