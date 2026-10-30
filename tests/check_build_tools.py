#!/usr/bin/env python3
"""Host-only provenance/export contracts; no compiler or device is invoked."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('export_artifacts', ROOT / 'tools/export_artifacts.py')
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)
record_spec = importlib.util.spec_from_file_location('build_record', ROOT / 'tools/build_record.py')
recorder = importlib.util.module_from_spec(record_spec)
record_spec.loader.exec_module(recorder)


class InputTests(unittest.TestCase):
    def test_export_header_tamper_and_extra_header_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            header = root / 'include/opensolo/mavlink/solo/mavlink.h'
            header.parent.mkdir(parents=True)
            header.write_text('/* pinned header */')
            contract_path = ROOT / 'vendor/mavlink/contract.json'
            contract = json.loads(contract_path.read_text())
            manifest = {'schema': 1, 'wire_protocol': '1.0', 'generator_version': contract['generator_version'],
                        'contract': contract, 'contract_sha256': recorder.digest(contract_path),
                        'exporter_sha256': recorder.digest(ROOT / 'tools/export_mavlink.py'),
                        'header_sha256': {str(header.relative_to(root)): hashlib.sha256(header.read_bytes()).hexdigest()}}
            (root / 'manifest.json').write_text(json.dumps(manifest))
            self.assertEqual(recorder.mavlink_inventory(root)['manifest'], manifest)
            header.write_text('/* changed */')
            with self.assertRaisesRegex(ValueError, 'inventory/hash'):
                recorder.mavlink_inventory(root)
            header.write_text('/* pinned header */')
            (header.parent / 'unexpected.h').write_text('/* extra */')
            with self.assertRaisesRegex(ValueError, 'inventory/hash'):
                recorder.mavlink_inventory(root)

    def test_untracked_source_rejected_even_when_git_ignores_it(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src').mkdir()
            (root / 'src/untracked.cpp').write_text('int injected;')
            with mock.patch.object(recorder, 'run', return_value=''):
                with self.assertRaisesRegex(ValueError, 'Untracked or ignored build input'):
                    recorder.inventory(root)

    def test_tracked_content_change_changes_inventory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'src').mkdir()
            path = root / 'src/owned.cpp'
            path.write_text('int first;')
            def git(args, root=None):
                return 'src/owned.cpp\0' if 'ls-files' in args else ('revision' if 'rev-parse' in args else '')
            with mock.patch.object(recorder, 'run', side_effect=git):
                before = recorder.inventory(root)
                path.write_text('int second;')
                after = recorder.inventory(root)
            self.assertNotEqual(before['files'], after['files'])


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        self.build.mkdir()
        self.record = {'version': '5.0.0-dev+g123-bench-noidle', 'artifacts': {}}
        for name in ('artoo.bin', 'artoo.elf', 'artoo.hex', 'artoo.map'):
            data = self.record['version'].encode() + b'\0' + name.encode()
            (self.build / name).write_bytes(data)
            self.record['artifacts'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
        self.write_manifest()

    def write_manifest(self):
        (self.build / 'build-manifest.json').write_text(json.dumps(self.record))

    def test_exact_copy_and_collision_preserved(self):
        destination = self.root / 'saved'
        exporter.export(self.build, destination)
        expected = (destination / ('artoo_' + self.record['version'] + '.bin')).read_bytes()
        self.assertEqual(expected, (self.build / 'artoo.bin').read_bytes())
        self.assertFalse((destination / 'INCOMPLETE').exists())
        with self.assertRaises(FileExistsError):
            exporter.export(self.build, destination)
        self.assertEqual(expected, (destination / ('artoo_' + self.record['version'] + '.bin')).read_bytes())

    def test_tampered_input_never_claims_destination(self):
        (self.build / 'artoo.bin').write_bytes(b'changed')
        with self.assertRaises(ValueError):
            exporter.export(self.build, self.root / 'saved')
        self.assertFalse((self.root / 'saved').exists())

    def test_unsafe_version_rejected(self):
        self.record['version'] = '../escape'
        self.write_manifest()
        with self.assertRaises(ValueError):
            exporter.export(self.build, self.root / 'saved')

    def test_manifest_must_describe_embedded_version(self):
        self.record['version'] = '5.0.0-dev+wrong'
        self.write_manifest()
        with self.assertRaises(ValueError):
            exporter.export(self.build, self.root / 'saved')


if __name__ == '__main__':
    unittest.main()
