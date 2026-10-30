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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=('prepare', 'finish'))
    for name in ('root', 'build', 'profile', 'board'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--cc')
    parser.add_argument('--cxx')
    parser.add_argument('--host-cc', default='/usr/bin/gcc')
    args = parser.parse_args()
    root, build = Path(args.root).resolve(), Path(args.build).resolve()
    if args.profile not in ('production', 'factory', 'bench') or args.board not in ('dvt', 'bb03'):
        raise ValueError('Invalid profile/board')
    state = inventory(root)
    version_args = ['--bench-noidle'] if args.profile == 'bench' else []
    version = run([root / 'git_describe_tag', *version_args], root)
    input_path = build / 'build-inputs.json'
    if args.action == 'prepare':
        (build / 'generated').mkdir(parents=True, exist_ok=True)
        version_src = '#include "version.h"\nconst char* Version::str() {return ' + json.dumps(version) + ';}\n'
        replace_changed(build / 'generated/version.cpp', version_src)
        state.update(version=version, profile=args.profile, board=args.board,
                     host=host_environment(args.host_cc), toolchain=target_environment(args.cc, args.cxx))
        replace_changed(input_path, json.dumps(state, indent=2) + '\n')
        return
    prepared = json.loads(input_path.read_text())
    if any(prepared[k] != state[k] for k in state) or prepared['version'] != version:
        raise ValueError('Source changed during build; freeze candidate and rebuild')
    host, target = host_environment(args.host_cc), target_environment(args.cc, args.cxx)
    if prepared['host'] != host or prepared['toolchain'] != target:
        raise ValueError('Build tools changed during build')
    commands = json.loads((build / 'compile_commands.json').read_text())
    flags = ['-DBOARD=' + ('3' if args.board == 'dvt' else '2'),
             '-DARTOO_FACTORY_TESTS=' + str(int(args.profile == 'factory')),
             '-DARTOO_BENCH_NO_IDLE=' + str(int(args.profile == 'bench'))]
    for command in commands:
        argv = command.get('arguments') or shlex.split(command['command'])
        if not all(flag in argv for flag in flags):
            raise ValueError('Compilation flags do not match profile')
    binary = (build / 'artoo.bin').read_bytes()
    if version.encode('ascii') + b'\0' not in binary:
        raise ValueError('Selected version absent from firmware')
    if len(binary) > 0x3f800:
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
        'factory_tests': args.profile == 'factory', 'bench_no_idle': args.profile == 'bench',
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
