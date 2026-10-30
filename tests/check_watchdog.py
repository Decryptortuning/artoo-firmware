#!/usr/bin/env python3
import pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='artoo-watchdog-') as tmp:
    exe=pathlib.Path(tmp)/'watchdog'
    subprocess.run(['g++','-std=c++11','-DARTOO_HOST_TEST','-DARTOO_EXPERIMENTAL_IWDG=1','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie','-I',str(root),str(root/'tests/watchdog.cpp'),str(root/'src/watchdog.cpp'),str(root/'src/stm32/rcc.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
