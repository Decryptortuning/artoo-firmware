#!/usr/bin/env python3
"""Native actual idle timer, with inert input/output sinks; no hardware writes."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="artoo-idle-") as directory:
    # Test the undefined-macro fallback as well as both explicit build values.
    for mode in (None, 0, 1):
        exe = pathlib.Path(directory) / ("idle-" + str(mode))
        flags = [] if mode is None else [f"-DARTOO_BENCH_NO_IDLE={mode}"]
        subprocess.run([
            "g++", "-std=c++11", "-DARTOO_HOST_TEST", "-funsigned-char", "-O1", "-g",
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
    helper = package / "scripts/versioned-binaries.sh"
    shutil.copy2(root / "scripts/versioned-binaries.sh", helper)
    version = "5.0.0-dev+g0123456789ab-bench-noidle"
    artifacts = {}
    for name in ("artoo.bin", "artoo.elf", "artoo.hex"):
        data = version.encode() + b"\0fixture-" + name.encode()
        (package / name).write_bytes(data)
        artifacts[name] = {"sha256": hashlib.sha256(data).hexdigest()}
    (package / "build-manifest.json").write_text(json.dumps({"version": version, "artifacts": artifacts}))
    subprocess.run(["sh", str(helper)], check=True)
    for name in artifacts:
        assert (package / "bins" / ("artoo_" + version + pathlib.Path(name).suffix)).read_bytes() == (package/name).read_bytes()
    (package / "artoo.bin").write_bytes(b"changed")
    assert subprocess.run(["sh", str(helper)], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode != 0
print("Manifest-bound packaging preserved bench suffix and rejected changed artifact.")
