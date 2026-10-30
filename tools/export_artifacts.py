#!/usr/bin/env python3
"""Copy a manifest-bound build into a new immutable export directory."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys


def export(build, destination):
    build, destination = Path(build).resolve(), Path(destination).resolve()
    manifest_bytes = (build / 'build-manifest.json').read_bytes()
    record = json.loads(manifest_bytes)
    version = record['version']
    if not re.fullmatch(r'[A-Za-z0-9.+-]+', version):
        raise ValueError('Invalid manifest version')
    files = {}
    for name in ('artoo.bin', 'artoo.elf', 'artoo.hex', 'artoo.map'):
        data = (build / name).read_bytes()
        expected = record['artifacts'][name]
        if hashlib.sha256(data).hexdigest() != expected['sha256'] or len(data) != expected['bytes']:
            raise ValueError('Artifact differs from manifest: ' + name)
        files[name] = data
    if version.encode('ascii') + b'\0' not in files['artoo.bin']:
        raise ValueError('Manifest version is absent from binary')
    # mkdir is the exclusive claim; never remove or replace an existing export.
    # If writing fails the marker remains, so incomplete exports cannot look done.
    destination.mkdir(parents=False, exist_ok=False)
    marker = destination / 'INCOMPLETE'
    marker.write_text('Export in progress; do not deploy.\n')
    for name, data in files.items():
        target = destination / ('artoo_' + version + Path(name).suffix)
        with target.open('xb') as stream:
            stream.write(data)
        if hashlib.sha256(target.read_bytes()).hexdigest() != record['artifacts'][name]['sha256']:
            raise ValueError('Export readback differs: ' + name)
    (destination / 'build-manifest.json').write_bytes(manifest_bytes)
    # Input identity/commands are embedded in the manifest. Extra evidence is
    # useful but not substituted for that manifest's artifact authority.
    for name in ('build-inputs.json', 'compile_commands.json'):
        if (build / name).is_file():
            shutil.copyfile(build / name, destination / name)
    marker.unlink()
    return destination


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: export_artifacts.py BUILD NEW_EXPORT_DIRECTORY')
    print(export(sys.argv[1], sys.argv[2]))
