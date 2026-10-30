#!/usr/bin/env python3
"""Post-link SRAM containment proof obligations; no physical flash claim."""
import argparse,json,pathlib,re,subprocess
p=argparse.ArgumentParser();p.add_argument('--elf',required=True);a=p.parse_args()
symbols={}
for line in subprocess.check_output(['arm-none-eabi-nm','-n','-S',a.elf],text=True).splitlines():
    f=line.split()
    if len(f)==4: symbols[f[3]]=(int(f[0],16),int(f[1],16))
text=subprocess.check_output(['arm-none-eabi-objdump','-d','-j','.data',a.elf],text=True)
rows={}
for name in ['artoo_flash_operation','artoo_flash_terminal','artoo_flash_exception']:
    start,size=symbols[name];assert 0x20000000<=start<start+size<=0x2000c000
    lines=[]
    for line in text.splitlines():
        m=re.match(r'\s*([0-9a-f]+):',line)
        if not m or not start<=int(m[1],16)<start+size: continue
        lines.append(line)
        target=re.search(r'\b(?:bl|blx|b\.w)\s+([0-9a-f]+)\s+<',line)
        if target: assert 0x20000000<=int(target[1],16)<0x2000c000,line
        assert not re.search(r'\bblx\s+r',line),line
    assert lines
    rows[name]={'address':hex(start),'bytes':size,'disassembly':'\n'.join(lines)}
assert 'msr' in rows['artoo_flash_operation']['disassembly']
assert '0800' not in rows['artoo_flash_terminal']['disassembly'] # reviewed separately with literal pools
print(json.dumps({'passed':True,'ram_functions':rows,'limit':'Link/branch evidence; inspect literal pools and veneers; physical NMI/stall/IWDG behavior not established'},indent=2))
