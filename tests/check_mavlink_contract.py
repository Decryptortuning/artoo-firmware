#!/usr/bin/env python3
"""Compile independent fixture assertions against retained and new MCU headers."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--mavlink-export',type=Path,required=True)
p.add_argument('--contract',type=Path,required=True)
p.add_argument('--report',type=Path,required=True)
args=p.parse_args()
contract=json.loads(args.contract.read_text())
assert contract['wire_protocol']=='1.0'
checks=[]
def check(expr): checks.append('static_assert('+expr+', "'+expr+'");')
for ident,message in contract['messages'].items():
    name=message['name']; prefix='MAVLINK_MSG_ID_'+name
    for suffix,value in [('',int(ident)),('_LEN',message['length']),('_CRC',message['crc_extra'])]:
        check(prefix+suffix+' == '+str(value))
    ctype='mavlink_'+name.lower()+'_t'
    for field,layout in message['c_layout'].items():
        check('offsetof('+ctype+', '+field+') == '+str(layout['offset']))
        check('sizeof((('+ctype+'*)0)->'+field+') == '+str(layout['size']))
for name,value in contract['existing_enum_values'].items(): check(name+' == '+str(value))
results={}
with tempfile.TemporaryDirectory() as tmp:
    for label,include,header in [('retained',ROOT/'src','mavlink/c_library/ardupilotmega/mavlink.h'),
                                 ('export',args.mavlink_export/'include','opensolo/mavlink/solo/mavlink.h')]:
        cpp=Path(tmp)/(label+'.cpp')
        cpp.write_text('#include <stddef.h>\n#include <'+header+'>\n'+'\n'.join(checks)+'\nint main() {}\n')
        result=subprocess.run(['g++','-std=c++11','-I'+str(include),'-fsyntax-only',str(cpp)],capture_output=True,text=True)
        results[label]={'exit_code':result.returncode,'stderr':result.stderr,'assertions':len(checks)}
args.report.write_text(json.dumps({'fixture_sha256':hashlib.sha256(args.contract.read_bytes()).hexdigest(),
                                  'message_count':len(contract['messages']),'results':results},indent=2)+'\n')
if any(r['exit_code'] for r in results.values()): raise SystemExit('MAVLink contract mismatch; see report')
print('Retained and exported headers match independent message, enum and field layout assertions')
