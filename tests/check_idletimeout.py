#!/usr/bin/env python3
"""Native actual idle timer, with inert input/output sinks; no hardware writes."""
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
from mavlink_fixture import include_flags
MAVLINK_FLAGS = include_flags()

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-idle-") as directory:
    # Test the undefined-macro fallback as well as both explicit build values.
    for mode in (None, 0, 1):
        exe = pathlib.Path(directory) / ("idle-" + str(mode))
        flags = [] if mode is None else [f"-DARTOO_BENCH_NO_IDLE={mode}"]
        subprocess.run([
            "g++", *MAVLINK_FLAGS, "-std=c++11", "-DARTOO_HOST_TEST", "-funsigned-char", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-I", str(root),
            *flags, str(root / "tests/idletimeout.cpp"), "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
normal = subprocess.check_output(["./git_describe_tag"], cwd=root, text=True).strip()
bench = subprocess.check_output(["./git_describe_tag", "--bench-noidle"], cwd=root, text=True).strip()
assert bench == normal + "-bench-noidle"
assert subprocess.run(["./git_describe_tag", "--invalid"], cwd=root,
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
print("Default and explicit idle policies and distinct bench version passed.")
# Exercise actual packaging helper in a disposable tree, not qualified outputs.
with tempfile.TemporaryDirectory(prefix="artoo-idle-package-") as directory:
    package = pathlib.Path(directory)
    (package / "scripts").mkdir()
    (package / "tools").mkdir()
    build = package / "build"
    build.mkdir()
    destination = package / "saved"
    helper = package / "scripts/versioned-binaries.sh"
    shutil.copy2(root / "scripts/versioned-binaries.sh", helper)
    shutil.copy2(root / "tools/export_artifacts.py", package / "tools/export_artifacts.py")
    environment = dict(os.environ, ARTOO_PYTHON=sys.executable)
    version = "5.0.0-dev+g0123456789ab-bench-noidle"
    artifacts = {}
    for name in ("artoo.bin", "artoo.elf", "artoo.hex", "artoo.map"):
        data = version.encode() + b"\0fixture-" + name.encode()
        (build / name).write_bytes(data)
        artifacts[name] = {"sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)}
    (build / "build-manifest.json").write_text(json.dumps({"version": version, "artifacts": artifacts}))
    subprocess.run([str(helper), str(build), str(destination)], env=environment, check=True)
    for name in artifacts:
        assert (destination / ("artoo_" + version + pathlib.Path(name).suffix)).read_bytes() == (build/name).read_bytes()
    assert not (destination / "INCOMPLETE").exists()
    # An existing export is retained, including a matching artifact set.
    assert subprocess.run([str(helper), str(build), str(destination)], env=environment,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0
    saved = (destination / ("artoo_" + version + ".bin")).read_bytes()
    (build / "artoo.bin").write_bytes(b"changed")
    rejected = package / "rejected"
    assert subprocess.run([str(helper), str(build), str(rejected)], env=environment, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0
    assert not rejected.exists()
    assert (destination / ("artoo_" + version + ".bin")).read_bytes() == saved
print("Manifest-bound packaging preserved bench suffix, rejected collisions and changed artifacts.")
