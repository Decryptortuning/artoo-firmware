"""Deterministic host-only pinned C compression; no Tamp Python package needed."""
import ctypes
import os
import hashlib
import json
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
class Encoder:
    def __init__(self):
        vendor=ROOT/'vendor/tamp'
        provenance=json.loads((vendor/'UPSTREAM.json').read_text())
        for name,digest in provenance['sha256'].items():
            if hashlib.sha256((vendor/name).read_bytes()).hexdigest()!=digest:
                raise ValueError(f'vendored Tamp source differs from pin: {name}')
        self.directory=tempfile.TemporaryDirectory(prefix='artoo-tamp-encoder-')
        libpath=pathlib.Path(self.directory.name)/'encoder.so'
        subprocess.run([os.environ.get('ARTOO_HOST_CC', 'gcc'),'-std=c99','-O2','-shared','-fPIC','-include',str(ROOT/'src/tampconfig.h'),
                        str(ROOT/'tools/tampencode.c'),str(vendor/'common.c'),str(vendor/'compressor.c'),
                        '-o',str(libpath)],check=True)
        self.library=ctypes.CDLL(str(libpath))
        self.encode=self.library.artoo_tamp_encode
        self.encode.restype=ctypes.c_long
        self.encode.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint]
    def compress(self,data):
        data=bytes(data)
        output=ctypes.create_string_buffer(len(data)*3+1024)
        literal=max(5,max(data,default=0).bit_length())
        length=self.encode(data,len(data),output,len(output),literal)
        if length<0:raise ValueError('Tamp compression failed')
        return output.raw[:length]
    def pack(self,rle,indices):
        # Deterministic tie preference: ordinary RLE, then Tamp indices, then Tamp RLE.
        options=[bytes([0])+bytes(rle)]
        for codec,payload in ((1,bytes(indices)),(2,bytes(rle))):
            if len(payload)>0xffffff:raise ValueError('decoded budget exceeds 24 bits')
            options.append(bytes([codec])+len(payload).to_bytes(3,'little')+self.compress(payload))
        return min(options,key=len)
