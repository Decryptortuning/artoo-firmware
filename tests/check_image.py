#!/usr/bin/env python3
"""Read-only linked-image contracts and compiler stack evidence, no hardware."""
import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
parser = argparse.ArgumentParser()
parser.add_argument('--elf',type=pathlib.Path,required=True)
parser.add_argument('--binary',type=pathlib.Path,required=True)
parser.add_argument('--stack-dir',type=pathlib.Path,required=True)
parser.add_argument('--factory',action='store_true')
args = parser.parse_args()
binary = args.binary.read_bytes()
symbols = {}
for line in subprocess.check_output(['arm-none-eabi-nm','-n',str(args.elf)],text=True).splitlines():
    fields = line.split()
    if len(fields) == 3: symbols[fields[2]] = int(fields[0],16)
base,limit,ram,top = 0x08000000,0x0803f800,0x20000000,0x2000c000
vectors = struct.unpack_from('<76I',binary)
assert vectors[0] == top and vectors[1] == (symbols['_start'] | 1)
assert all(value == 0 or (value & 1 and base <= value < base+len(binary)) for value in vectors[1:])
assert base+len(binary) == symbols['__data_src']+symbols['__data_end']-symbols['__data_start'] <= limit
assert symbols['__heap_start'] == symbols['__bss_end'] <= symbols['__heap_end']
assert symbols['__heap_end'] == top-4096 and symbols['_stack'] == top
constructors = []
for group in ('preinit','init'):
    start,end = symbols[f'__{group}_array_start'],symbols[f'__{group}_array_end']
    assert start % 4 == 0 and (end-start)%4 == 0
    addresses = struct.unpack('<'+'I'*((end-start)//4),binary[start-base:end-base])
    assert all(address & 1 and base <= address < base+len(binary) for address in addresses)
    constructors.append({'group':group,'count':len(addresses)})
sections = subprocess.check_output(['arm-none-eabi-readelf','-S',str(args.elf)],text=True)
assert '.ARM.attributes' in sections
names = subprocess.check_output(['arm-none-eabi-nm','-C',str(args.elf)],text=True)
for handler in ('FactoryTest::onOutputTest','FactoryTest::onGpioTest','SelfTest::checkForShorts'):
    assert (handler in names) == args.factory, handler
assert ' _sbrk' in names
assert 'tamp_compressor_' not in names, 'host compressor linked into firmware'
assert not re.search(r'__atomic_\w+_[1248]',names), 'unexpected out-of-line atomic runtime'
frames = []
for path in args.stack_dir.glob('*.su'):
    for line in path.read_text().splitlines():
        fields = line.split('\t')
        if len(fields) == 3:
            frames.append({'function':fields[0],'bytes':int(fields[1]),'kind':fields[2]})
assert frames, 'compile with -fstack-usage'
frames.sort(key=lambda row:row['bytes'],reverse=True)
assert frames[0]['bytes'] < 4096, 'one frame exhausts minimum stack reserve'
print(json.dumps({'binary_sha256':hashlib.sha256(binary).hexdigest(),
    'binary_bytes':len(binary),'flash_gap':limit-base-len(binary),
    'static_ram_bytes':symbols['__bss_end']-ram,'heap_capacity':symbols['__heap_end']-symbols['__heap_start'],
    'minimum_stack_reserve':4096,'constructors':constructors,'factory_profile':args.factory,
    'stack_functions_reported':len(frames),'largest_static_frames':frames[:20],
    'stack_limitations':'All compiled functions, including linker-discarded ones; per-function compiler evidence only; no complete call-chain, ISR nesting, dynamic stack or physical high-water qualification.'},indent=2))
