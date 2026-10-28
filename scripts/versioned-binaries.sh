#!/bin/sh

set -eu
cd -- "$(dirname -- "$0")/.."
PROG=artoo

# Use the manifest bound to these artifacts, never the current checkout name.
VERSION=$(python3 - <<'PYVERSION'
import hashlib, json, pathlib, re
record = json.loads(pathlib.Path("build-manifest.json").read_text())
version = record["version"]
if not re.fullmatch(r"[A-Za-z0-9.+-]+", version):
    raise SystemExit("Invalid manifest version")
for name in ("artoo.bin", "artoo.elf", "artoo.hex"):
    data = pathlib.Path(name).read_bytes()
    if hashlib.sha256(data).hexdigest() != record["artifacts"][name]["sha256"]:
        raise SystemExit("Artifact differs from manifest: " + name)
if version.encode("ascii") + b"\0" not in pathlib.Path("artoo.bin").read_bytes():
    raise SystemExit("Manifest version is absent from binary")
print(version)
PYVERSION
)
DEST=bins

rm -rf ${DEST}
mkdir ${DEST}

cp ${PROG}.elf ${DEST}/${PROG}_${VERSION}.elf
cp ${PROG}.bin ${DEST}/${PROG}_${VERSION}.bin
cp ${PROG}.hex ${DEST}/${PROG}_${VERSION}.hex

cp build-manifest.json ${DEST}/${PROG}_${VERSION}.json

echo copied ${VERSION} binaries to ${DEST}
