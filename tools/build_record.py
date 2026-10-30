#!/usr/bin/env python3
"""Build inputs and exact artifacts, shared by isolated CMake profiles.

No network or device access. Preparation refuses untracked/ignored compilation
inputs; completion rejects source changes during a build. All subprocesses use
argv arrays. This is provenance, not hardware qualification.
"""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def run(args, root=None):
    return subprocess.check_output([str(x) for x in args], cwd=root, text=True).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_changed(path, text):
    if not path.exists() or path.read_text() != text:
        temporary = path.with_suffix(path.suffix + '.tmp')
        temporary.write_text(text)
        temporary.replace(path)


def inventory(root):
    paths = run(['git', 'ls-files', '-z'], root).split('\0')
    tracked = set(paths)
    # These trees supply headers, source, compression, fonts and generator code.
    # Ignore only interpreter bytecode, never an ignored source/header input.
    for directory in ('src', 'vendor', 'tools', 'resources', 'cmake', 'configs', 'scripts'):
        for path in (root / directory).rglob('*'):
            if not path.is_file() or '__pycache__' in path.parts or path.suffix == '.pyc':
                continue
            if str(path.relative_to(root)) not in tracked:
                raise ValueError('Untracked or ignored build input: ' + str(path))
    files = {}
    for name in sorted(paths):
        if not name:
            continue
        path = root / name
        if path.is_file():
            files[name] = {'sha256': digest(path), 'bytes': path.stat().st_size}
        else:
            raise ValueError('Missing or non-file tracked input: ' + name)
    return {
        'source_revision': run(['git', 'rev-parse', 'HEAD'], root),
        'source_dirty': bool(run(['git', 'diff', 'HEAD', '--'], root)),
        'files': files,
    }


def tool(path):
    found = shutil.which(str(path))
    if not found:
        raise ValueError('Missing tool: ' + str(path))
    executable = Path(found).resolve()
    return {'path': str(path), 'resolved': str(executable),
            'sha256': digest(executable), 'version': run([path, '--version'])}


def host_environment(host_cc):
    import PIL
    from PIL import features
    import PIL._imaging
    import PIL._imagingft
    return {
        'python': {'executable': sys.executable, 'version': sys.version,
                   'resolved': str(Path(sys.executable).resolve()),
                   'sha256': digest(Path(sys.executable).resolve())},
        'pillow': {'version': PIL.__version__, 'freetype': features.version('freetype2'),
                   'imaging_sha256': digest(Path(PIL._imaging.__file__)),
                   'imagingft_sha256': digest(Path(PIL._imagingft.__file__)),
                   'installed_files': {str(p): digest(Path(p.locate()))
                     for p in importlib.metadata.files('Pillow')
                     if Path(p.locate()).is_file() and not str(p).endswith('.pyc')}},
        'installed_python_packages': run([sys.executable, '-m', 'pip', 'freeze', '--all']),
        'native_compiler': tool(host_cc),
    }


def target_environment(cc, cxx):
    flags = ['-mthumb', '-mcpu=cortex-m3', '-msoft-float']
    libraries = {}
    for name in ('nano.specs', 'libc.a', 'libc_nano.a', 'libm.a', 'libgcc.a', 'libnosys.a'):
        path = Path(run([cc, *flags, '-print-file-name=' + name])).resolve()
        if not path.is_file():
            raise ValueError('Missing selected compiler runtime: ' + name)
        libraries[name] = {'path': str(path), 'sha256': digest(path)}
    subprograms = {}
    for name in ('cc1', 'cc1plus', 'as', 'ld'):
        path = run([cxx if name == 'cc1plus' else cc, '-print-prog-name=' + name])
        subprograms[name] = tool(path)
    package_info = subprocess.run(['dpkg-query', '-W', '-f=${Package} ${Version}\n',
        'gcc-arm-none-eabi', 'binutils-arm-none-eabi', 'libnewlib-arm-none-eabi'],
        capture_output=True, text=True)
    return {'c': tool(cc), 'cxx': tool(cxx), 'subprograms': subprograms,
            'multilib': run([cc, *flags, '-print-multi-directory']),
            'all_multilibs': run([cc, '-print-multi-lib']), 'libraries': libraries,
            'distro_packages_observed': package_info.stdout,
            'distro_packages_status': package_info.returncode,
            'note': 'Distro inventory is supplemental; resolved tool/runtime hashes identify selected inputs.'}


def mavlink_inventory(directory):
    root = Path(directory).resolve()
    owned = Path(__file__).resolve().parents[1]
    manifest_path = root / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    contract_path = owned / 'vendor/mavlink/contract.json'
    contract = json.loads(contract_path.read_text())
    if (manifest.get('schema') != 1 or manifest.get('wire_protocol') != '1.0'
            or manifest.get('generator_version') != contract['generator_version']
            or manifest.get('contract') != contract
            or manifest.get('contract_sha256') != digest(contract_path)
            or manifest.get('exporter_sha256') != digest(owned / 'tools/export_mavlink.py')):
        raise ValueError('MAVLink export provenance differs from owned contract/exporter')
    headers = manifest['header_sha256']
    if not isinstance(headers, dict) or not headers:
        raise ValueError('Empty MAVLink header manifest')
    actual = {str(p.relative_to(root)): digest(p) for p in (root / 'include').rglob('*') if p.is_file()}
    if actual != headers:
        raise ValueError('MAVLink export header inventory/hash differs from manifest')
    for name in headers:
        path = (root / name).resolve()
        if not path.is_relative_to(root / 'include') or not path.is_file():
            raise ValueError('MAVLink manifest path escapes include root')
    if 'include/opensolo/mavlink/solo/mavlink.h' not in headers:
        raise ValueError('Solo MAVLink entry point is missing')
    return {'path': str(root), 'manifest_sha256': digest(manifest_path), 'manifest': manifest}


def archive_inventory(root, toolchain_archive, pillow_wheel, host, target):
    pins = json.loads((root / 'dependency-pins.json').read_text())
    archives = {}
    for key, supplied in [('arm-15.3.rel2', toolchain_archive), ('pillow-13.0.0', pillow_wheel)]:
        if supplied:
            path = Path(supplied).resolve()
            pin = pins[key]
            if digest(path) != pin['sha256'] or path.stat().st_size != pin['bytes']:
                raise ValueError('Dependency archive differs from source pin: ' + key)
            archives[key] = {'path': str(path), 'pin': pin}
    if host['pillow']['version'] != '10.2.0':
        if host['pillow']['version'] != '13.0.0' or 'pillow-13.0.0' not in archives:
            raise ValueError('Non-baseline Pillow requires the reviewed candidate wheel')
    if '15.3' in target['c']['version'] and 'arm-15.3.rel2' not in archives:
        raise ValueError('Arm15.3 evaluation requires the reviewed complete archive')
    return {'pins_sha256': digest(root / 'dependency-pins.json'), 'archives': archives,
            'scope': 'Archive checksum plus actual installed tool/runtime hashes; no signature verification or automatic installation.'}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('prepare', 'finish'))
    parser.add_argument('--experimental-iwdg', type=int, choices=(0,1), default=0)
    for name in ('root', 'build', 'profile', 'board'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--cc')
    parser.add_argument('--cxx')
    parser.add_argument('--host-cc', default='/usr/bin/gcc')
    parser.add_argument('--mavlink-export', required=True)
    parser.add_argument('--toolchain-archive', default='')
    parser.add_argument('--pillow-wheel', default='')
    args = parser.parse_args()
    root, build = Path(args.root).resolve(), Path(args.build).resolve()
    if args.experimental_iwdg and args.profile != 'bench':
        raise ValueError('IWDG requires bench')
    if args.profile not in ('production', 'factory', 'bench') or args.board not in ('dvt', 'bb03'):
        raise ValueError('Invalid profile/board')
    state = inventory(root)
    host, target = host_environment(args.host_cc), target_environment(args.cc, args.cxx)
    external = {'mavlink_export': mavlink_inventory(args.mavlink_export),
                'dependency_archives': archive_inventory(root, args.toolchain_archive, args.pillow_wheel, host, target)}
    version_args = ['--bench-noidle'] if args.profile == 'bench' else []
    version = run([root / 'git_describe_tag', *version_args], root)
    if args.experimental_iwdg: version += '-iwdg'
    input_path = build / 'build-inputs.json'
    if args.action == 'prepare':
        (build / 'generated').mkdir(parents=True, exist_ok=True)
        version_src = '#include "version.h"\nconst char* Version::str() {return ' + json.dumps(version) + ';}\n'
        replace_changed(build / 'generated/version.cpp', version_src)
        state.update(version=version, profile=args.profile, board=args.board,
                     host=host, toolchain=target, **external)
        replace_changed(input_path, json.dumps(state, indent=2) + '\n')
        return
    prepared = json.loads(input_path.read_text())
    if any(prepared[k] != state[k] for k in state) or prepared['version'] != version:
        raise ValueError('Source changed during build; freeze candidate and rebuild')
    if prepared['host'] != host or prepared['toolchain'] != target:
        raise ValueError('Build tools changed during build')
    if any(prepared[key] != value for key, value in external.items()):
        raise ValueError('External build inputs changed during build')
    commands = json.loads((build / 'compile_commands.json').read_text())
    flags = ['-DBOARD=' + ('3' if args.board == 'dvt' else '2'),
             '-DARTOO_FACTORY_TESTS=' + str(int(args.profile == 'factory')),
             '-DARTOO_BENCH_NO_IDLE=' + str(int(args.profile == 'bench')),
             '-DARTOO_EXPERIMENTAL_IWDG=' + str(args.experimental_iwdg)]
    for command in commands:
        argv = command.get('arguments') or shlex.split(command['command'])
        if not all(flag in argv for flag in flags):
            raise ValueError('Compilation flags do not match profile')
    binary = (build / 'artoo.bin').read_bytes()
    if version.encode('ascii') + b'\0' not in binary:
        raise ValueError('Selected version absent from firmware')
    if binary[304:320] != b'OS5JNL1!\x00\xf0\x03\x08\xac\x00\x01\x00':
        raise ValueError('Missing fixed journal storage layout marker')
    if len(binary) > 0x3f000:
        raise ValueError('Firmware overlaps reserved parameter page')
    names = ('artoo.bin', 'artoo.elf', 'artoo.hex', 'artoo.map')
    dependencies = {}
    for line in run(['ninja', '-C', build, '-t', 'deps']).splitlines():
        if line.startswith('    '):
            path = Path(line.strip())
            path = path if path.is_absolute() else build / path
            path = path.resolve()
            if not path.is_file():
                raise ValueError('Missing compiler dependency: ' + str(path))
            dependencies[str(path)] = digest(path)
    record = {**prepared, 'schema': 2, 'board_id': 3 if args.board == 'dvt' else 2,
        'factory_tests': args.profile == 'factory', 'bench_no_idle': args.profile == 'bench', 'experimental_iwdg': bool(args.experimental_iwdg),
        'parameter_storage': {'schema': 1, 'start': 0x0803f000, 'pages': 2, 'payload_bytes': 172},
        'build_system': 'cmake-ninja', 'host': host,
        'toolchain': target, 'compiler_dependencies': dependencies,
        'cmake': tool('cmake'), 'ninja': tool('ninja'), 'compile_commands': commands,
        'all_build_commands': run(['ninja', '-C', build, '-t', 'commands', 'artoo']),
        'config_path': 'configs/' + args.profile + '-' + args.board + '.config',
        'config_sha256': digest(root / 'configs' / (args.profile + '-' + args.board + '.config')),
        'generated': {name: digest(build / 'generated' / name) for name in ('resources-gen.cpp', 'resources-gen.h', 'version.cpp')},
        'artifacts': {name: {'bytes': (build / name).stat().st_size, 'sha256': digest(build / name)} for name in names}}
    replace_changed(build / 'build-manifest.json', json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
