#!/usr/bin/env python3
"""Actual mixed decoder vs immutable pre-compression generator RGB565 corpus."""
import collections
import importlib.util
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
from mavlink_fixture import include_flags
MAVLINK_FLAGS = include_flags()

root=pathlib.Path(__file__).resolve().parents[1]
candidate_python=os.environ.get('ARTOO_PYTHON',sys.executable)
baseline_python=str(root/'.venv/bin/python')
# The old renderer remains independently pinned even during a candidate Pillow
# upgrade; do not regenerate both sides with the new renderer and mask drift.
baseline_pillow=subprocess.check_output([baseline_python,'-c','import PIL;print(PIL.__version__)'],text=True).strip()
assert baseline_pillow=='10.2.0', 'Historical graphics baseline requires Pillow10.2.0'
BASELINE='66336f01c57de971a85c001aa627308871a3747f'
FLAGS=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
       '-fno-omit-frame-pointer','-fno-pie']
def arrays(source):
    return {name:[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',body)]
            for name,body in re.findall(r'static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};',source,re.S)}
def pixels(encoded):
    decoded=[];offset=0
    while offset<len(encoded):
        header=encoded[offset];offset+=1
        if header==128:
            assert offset==len(encoded)
            return decoded
        if header>128:
            decoded.extend([encoded[offset]]*(257-header));offset+=1
        else:
            decoded.extend(encoded[offset:offset+header+1]);offset+=header+1
    raise AssertionError('missing EOF')
with tempfile.TemporaryDirectory(prefix='artoo-graphics-') as directory:
    out=pathlib.Path(directory);baseline=out/'baseline';baseline.mkdir()
    for name in ('assetgen.py','assetlimits.py'):
        (baseline/name).write_bytes(subprocess.check_output(['git','show',f'{BASELINE}:tools/{name}'],cwd=root))
    subprocess.run([baseline_python,str(baseline/'assetgen.py'),str(root/'resources/assets.cfg'),str(baseline)],cwd=root,check=True)
    subprocess.run([candidate_python,str(root/'tools/assetgen.py'),str(root/'resources/assets.cfg'),str(out)],cwd=root,check=True)
    source=(out/'resources-gen.cpp').read_text(); old=(baseline/'resources-gen.cpp').read_text()
    current=arrays(source);original=arrays(old)
    repeated=out/'repeated';repeated.mkdir()
    subprocess.run([candidate_python,str(root/'tools/assetgen.py'),str(root/'resources/assets.cfg'),str(repeated)],cwd=root,check=True)
    assert arrays((repeated/'resources-gen.cpp').read_text())==current, 'asset output is not deterministic'
    # The current generator must preserve every descriptor except its format.
    def metadata(text):
        return [(kind,name,re.sub(r'ImgFmt(?:Packed|Rle)','FORMAT',body)) for kind,name,body in
                re.findall(r'(?:extern const|static const) Gfx::(ImageAsset|FontAsset|GlyphAsset) (\w+)(?:\[\])? = \{(.*?)\};',text,re.S)]
    assert metadata(source)==metadata(old), 'geometry/descriptor contract changed'

    assert current.keys()==original.keys()
    palettes={name:re.search(r'(\w+)_data',body)[1]+'_data'
              for name,body in re.findall(r'extern const Gfx::ImagePalette (\w+) = \{(.*?)\};',source,re.S)}
    for name in palettes.values():assert current[name]==original[name],name
    cases=[]
    for name,body in re.findall(r'extern const Gfx::ImageAsset (\w+) = \{(.*?)\};',source,re.S):
        dimensions=re.findall(r'^\s+(\d+),',body,re.M);palette=re.search(r'&(\w+)',body)[1]
        cases.append((name+'_data',int(dimensions[0])*int(dimensions[1]),palette,f'{name}.data',f'{name}.encodedBytes',f'Gfx::drawImage(0,0,{name});',None))
    for name,body in re.findall(r'extern const Gfx::FontAsset (\w+) = \{(.*?)\};',source,re.S):
        palette=re.search(r'&(\w+)',body)[1]
        glyphs=re.search(rf'static const Gfx::GlyphAsset {name}_glyphs\[\] = \{{(.*?)\}};',source,re.S)[1]
        entries=re.findall(r'\{ (\w+), (\d+), (\d+), sizeof\(\w+\) \}',glyphs);assert entries
        for index,(data,width,height) in enumerate(entries):
            cases.append((data,int(width)*int(height),palette,f'{name}.glyphs[{index}].data',f'{name}.glyphs[{index}].encodedBytes',f'Gfx::drawGlyph(char({name}.start_ch+{index}),0,0,{name});',f'Gfx::drawGlyph(char({name}.start_ch+{index}),0,0,{name},&foreground,&background);'))
    checks=[];golden=bytearray();codecs=collections.Counter()
    for data,count,palette,pointer,length,render,tinted in cases:
        decoded=pixels(original[data]);assert len(decoded)==count,data
        offset=len(golden);colors=original[palettes[palette]]
        golden.extend(b''.join(struct.pack('<H',colors[index]) for index in decoded))
        checks.append(f'assert({length}==sizeof({data})); check({pointer},{length},{count},&{palette},{offset}); clearSink(); {render} checkSink({count},{offset});')
        if tinted: checks.append(f'clearSink(); {tinted} checkSink({count},{offset},true);')
        codecs[current[data][0]]+=1
    (out/'golden.bin').write_bytes(golden)
    fixture=out/'graphics.cpp'
    fixture.write_text('#include "resources-gen.cpp"\n#include "asset_fixture.h"\nint main(int argc,char **argv) {\n'
        'uint16_t foreground=0xf81f,background=0x1357; assert(argc==2); FILE *f=fopen(argv[1],"rb"); assert(f); int ch; while((ch=fgetc(f))!=EOF) golden.push_back(ch); fclose(f);\n'
        +'\n'.join(checks)+'\nadversarial();\n}\n')
    objects=[]
    for name in ('common','decompressor'):
        obj=out/(name+'.o');objects.append(str(obj))
        subprocess.run(['gcc','-std=c99',*FLAGS,'-include',str(root/'src/tampconfig.h'),'-c',str(root/f'vendor/tamp/{name}.c'),'-o',str(obj)],check=True)
    exe=out/'graphics'
    subprocess.run(['g++',*MAVLINK_FLAGS,'-std=c++11','-DARTOO_HOST_TEST','-funsigned-char',*FLAGS,'-no-pie',
                    '-I',str(root),'-I',str(root/'src'),'-I',str(root/'tests'),'-include',str(root/'tests/lcd_sink.h'),str(fixture),str(root/'src/assetdecoder.cpp'),str(root/'src/gfx.cpp'),*objects,'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(out/'golden.bin')],check=True)
    wrapper=out/'wrappers'
    subprocess.run(['g++',*MAVLINK_FLAGS,'-std=c++11','-DARTOO_HOST_TEST','-funsigned-char',*FLAGS,'-no-pie','-I',str(root),
                    '-include',str(root/'tests/lcd_sink.h'),str(root/'tests/graphics_wrappers.cpp'),
                    str(root/'src/gfx.cpp'),str(root/'src/assetdecoder.cpp'),*objects,'-o',str(wrapper)],check=True)
    subprocess.run([str(wrapper)],check=True)
    spec=importlib.util.spec_from_file_location('assetlimits',root/'tools/assetlimits.py')
    limits=importlib.util.module_from_spec(spec);spec.loader.exec_module(limits)
    limits.glyph(255,255,65535)
    for args in ((256,1,1),(1,256,1),(1,1,65536),(1,1,0),(-1,1,1)):
        try:limits.glyph(*args)
        except ValueError:pass
        else:raise AssertionError(args)
    limits.font(255,255,255,0,254)
    for args in ((256,1,1,0,255),(2,256,1,0,1),(2,1,256,0,1),(2,1,1,255,256)):
        try:limits.font(*args)
        except ValueError:pass
        else:raise AssertionError(args)
    print(f'{len(cases)} assets, {len(golden)//2} exact baseline RGB565 pixels, chunks1/7/32, truncation/header/corruption/quota/ISR/reentry and100k fuzz passed. Codecs: {dict(codecs)}')
