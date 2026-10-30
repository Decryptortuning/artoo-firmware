#!/usr/bin/env python3
"""Compare independent wire fixture and explicit old unused logging definitions."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import struct
import tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--mavlink-export',type=Path,required=True)
p.add_argument('--contract',type=Path,required=True)
p.add_argument('--report',type=Path,required=True)
args=p.parse_args()
contract=json.loads(args.contract.read_text())
legacy_path=ROOT/'tests/legacy_mavlink_differences.json'
legacy=json.loads(legacy_path.read_text())
assert contract['wire_protocol']=='1.0'
# The exact absent names must remain absent in the old generated source, and
# runtime code must not consume any migrated remote-log/component identifiers.
headers='\n'.join(f.read_text() for f in (ROOT/'src/mavlink/c_library').rglob('*.h'))
for name in legacy['absent_enums']:
    assert not re.search(r'\b'+name+r'\b', headers), name
for f in (ROOT/'src').rglob('*'):
    if f.suffix not in ('.cpp','.h') or 'mavlink' in f.relative_to(ROOT/'src').parts[:-1]: continue
    assert not re.search(r'REMOTE_LOG|MAV_COMP_ID_LOG',f.read_text()), str(f)

def assertions(retained):
    checks=[]
    def check(expr): checks.append('static_assert('+expr+', "'+expr+'");')
    for ident,message in contract['messages'].items():
        name=message['name']; prefix='MAVLINK_MSG_ID_'+name
        if retained and name in legacy['messages']:
            message=legacy['messages'][name]; ident=message['id']
        for suffix,value in [('',int(ident)),('_LEN',message['length']),('_CRC',message['crc_extra'])]:
            check(prefix+suffix+' == '+str(value))
        ctype='mavlink_'+name.lower()+'_t'
        for field,layout in message['c_layout'].items():
            check('offsetof('+ctype+', '+field+') == '+str(layout['offset']))
            check('sizeof((('+ctype+'*)0)->'+field+') == '+str(layout['size']))
    for name,value in contract['existing_enum_values'].items():
        if retained and name in legacy['absent_enums']: continue
        if retained: value=legacy['enum_values'].get(name,value)
        check(name+' == '+str(value))
    return checks
results={}
with tempfile.TemporaryDirectory() as tmp:
    for label,include,header in [('retained',ROOT/'src','mavlink/c_library/ardupilotmega/mavlink.h'),
                                 ('export',args.mavlink_export/'include','opensolo/mavlink/solo/mavlink.h')]:
        checks=assertions(label=='retained')
        cpp=Path(tmp)/(label+'.cpp')
        cpp.write_text('#include <stddef.h>\n#include <'+header+'>\n'+'\n'.join(checks)+'\nint main() {}\n')
        result=subprocess.run(['g++','-std=c++11','-I'+str(include),'-fsyntax-only',str(cpp)],capture_output=True,text=True)
        results[label]={'exit_code':result.returncode,'stderr':result.stderr,'assertions':len(checks)}
    # Independent deterministic payloads and CRC use fixture lengths/CRC extras,
    # never the generated serializer being qualified.
    frames=[]
    for seq,(ident,message) in enumerate(contract['messages'].items()):
        payload=bytes((int(ident)+i*17)&255 for i in range(message['length']))
        body=bytes([len(payload),seq&255,1,1,int(ident)])+payload
        crc=0xffff
        for value in body+bytes([message['crc_extra']]):
            crc ^= value
            for _ in range(8): crc=(crc>>1)^(0x8408 if crc&1 else 0)
        frames.append(b'\xfe'+body+struct.pack('<H',crc))
    golden=b''.join(frames)
    damaged=bytearray(frames[0]); damaged[-1]^=1
    executable=Path(tmp)/'wire-probe'
    build=subprocess.run(['g++','-std=c++11','-fsanitize=undefined','-fno-sanitize-recover=all',
                          '-Werror=address-of-packed-member','-Werror=ignored-qualifiers',
                          '-I'+str(args.mavlink_export/'include'),str(ROOT/'tests/mavlink_wire_probe.cpp'),
                          '-o',str(executable)],capture_output=True,text=True)
    results['wire_build']={'exit_code':build.returncode,'stderr':build.stderr}
    if not build.returncode:
        for label,stream in [('wire_all',golden),('wire_crc_recovery',bytes(damaged)+golden)]:
            result=subprocess.run([str(executable)],input=stream,capture_output=True,timeout=10)
            results[label]={'exit_code':result.returncode if result.returncode else int(result.stdout!=golden),
                            'frames':len(frames),'byte_identical':result.stdout==golden,
                            'golden_sha256':hashlib.sha256(golden).hexdigest(),
                            'stderr':result.stderr.decode(errors='replace')}
args.report.write_text(json.dumps({'fixture_sha256':hashlib.sha256(args.contract.read_bytes()).hexdigest(),
                                  'reviewed_legacy_sha256':hashlib.sha256(legacy_path.read_bytes()).hexdigest(),
                                  'message_count':len(contract['messages']),'results':results},indent=2)+'\n')
if any(r['exit_code'] for r in results.values()): raise SystemExit('MAVLink contract mismatch; see report')
print('Export matches full166-message contract; retained headers match exact reviewed old logging differences')
