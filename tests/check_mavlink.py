#!/usr/bin/env python3
import pathlib,struct,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
    exe=pathlib.Path(directory)/'checksum'
    subprocess.run(['g++','-std=c++11','-fsanitize=undefined','-fno-sanitize-recover=all','-Werror=address-of-packed-member','-Werror=ignored-qualifiers','-I',str(root),str(root/'tests/mavlink_checksum.cpp'),'-o',str(exe)],check=True)
    wire=bytes.fromhex(subprocess.check_output([str(exe)],text=True).strip())
payload=struct.pack('<IBBBBB',0,2,3,0,4,3)
body=bytes([9,0,1,1,0])+payload
crc=0xffff
for value in body+bytes([50]):
    crc^=value
    for _ in range(8): crc=(crc>>1)^ (0x8408 if crc&1 else 0)
assert wire==bytes([0xfe])+body+struct.pack('<H',crc),wire.hex()
print('MAVLink heartbeat wire CRC and unaligned checksum operations passed with UBSan.')
