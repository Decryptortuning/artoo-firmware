#!/usr/bin/env python3
"""Decode every generated image/glyph with the production bounded RLE policy.
Independent Python decoding supplies RGB565 checksums; no LCD is simulated.
"""
import importlib.util
import pathlib
import re
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]

def pixels(encoded):
    decoded = []
    offset = 0
    while offset < len(encoded):
        header = encoded[offset]
        offset += 1
        if header == 128:
            assert offset == len(encoded)
            return decoded
        if header > 128:
            decoded.extend([encoded[offset]] * (257-header))
            offset += 1
        else:
            decoded.extend(encoded[offset:offset+header+1])
            offset += header+1
    raise AssertionError("missing EOF")

with tempfile.TemporaryDirectory(prefix="artoo-graphics-") as directory:
    out = pathlib.Path(directory)
    subprocess.run([str(root / ".venv/bin/python"),str(root / "tools/assetgen.py"),
                    str(root / "resources/assets.cfg"),str(out)],cwd=root,check=True)
    source = (out / "resources-gen.cpp").read_text()
    arrays = {name:[int(x,16) for x in re.findall(r"0x([0-9a-fA-F]+)",body)]
              for name,body in re.findall(r"static const uint(?:8|16)_t (\w+)\[\] = \{(.*?)\};",source,re.S)}
    palettes = {name:re.search(r"(\w+)_data",body)[1]+"_data"
                for name,body in re.findall(r"extern const Gfx::ImagePalette (\w+) = \{(.*?)\};",source,re.S)}
    cases = []
    for name,body in re.findall(r"extern const Gfx::ImageAsset (\w+) = \{(.*?)\};",source,re.S):
        dimensions = re.findall(r"^\s+(\d+),",body,re.M)
        palette = re.search(r"&(\w+)",body)[1]
        cases.append((name+"_data",int(dimensions[0])*int(dimensions[1]),palette,
                      f"{name}.data",f"{name}.encodedBytes"))
    for name,body in re.findall(r"extern const Gfx::FontAsset (\w+) = \{(.*?)\};",source,re.S):
        palette = re.search(r"&(\w+)",body)[1]
        glyphs = re.search(rf"static const Gfx::GlyphAsset {name}_glyphs\[\] = \{{(.*?)\}};",source,re.S)[1]
        entries = re.findall(r"\{ (\w+), (\d+), (\d+), sizeof\(\w+\) \}",glyphs)
        assert entries
        for index,(data,width,height) in enumerate(entries):
            cases.append((data,int(width)*int(height),palette,
                          f"{name}.glyphs[{index}].data",f"{name}.glyphs[{index}].encodedBytes"))
    checks = []
    total_pixels = 0
    for data,count,palette,pointer,length in cases:
        decoded = pixels(arrays[data])
        assert len(decoded) == count, data
        colors = arrays[palettes[palette]]
        digest = 2166136261
        for index in decoded:
            digest = ((digest ^ colors[index])*16777619) & 0xffffffff
        total_pixels += count
        checks.append(f'assert({length} == sizeof({data})); check({pointer},{length},{count},&{palette},{digest}u);')
    fixture = out / "graphics.cpp"
    fixture.write_text('''#include <assert.h>
#include <stdint.h>
#include "resources-gen.cpp"
#include "rledecoder.h"
static void check(const void *bytes,unsigned len,unsigned count,const Gfx::ImagePalette *p,uint32_t expected) {
    uint32_t digest = 2166136261u;
    unsigned written = 0;
    bool ok = RleDecoder::decode(static_cast<const uint8_t*>(bytes),len,count,p->maxIdx,
        [&](uint8_t index,unsigned repeat) {
            written += repeat;
            while(repeat--) digest = (digest ^ p->colors[index])*16777619u;
        });
    assert(ok && written == count && digest == expected);
}
int main() {
''' + "\n".join(checks) + "\n}\n")
    spec = importlib.util.spec_from_file_location("assetlimits",root / "tools/assetlimits.py")
    limits = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(limits)
    limits.glyph(255,255,65535)
    for args in ((256,1,1),(1,256,1),(1,1,65536),(1,1,0),(-1,1,1)):
        try: limits.glyph(*args)
        except ValueError: pass
        else: raise AssertionError(args)
    limits.font(255,255,255,0,254)
    for args in ((256,1,1,0,255),(2,256,1,0,1),(2,1,256,0,1),(2,1,1,255,256)):
        try: limits.font(*args)
        except ValueError: pass
        else: raise AssertionError(args)
    wrapper = out / "graphics-wrappers"
    subprocess.run(["g++","-std=c++11","-DARTOO_HOST_TEST","-funsigned-char","-O1","-g",
                    "-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer",
                    "-fno-pie","-no-pie","-I",str(root),"-include",str(root / "tests/lcd_sink.h"),
                    str(root / "tests/graphics_wrappers.cpp"),str(root / "src/gfx.cpp"),"-o",str(wrapper)],check=True)
    subprocess.run([str(wrapper)],check=True)
    exe = out / "graphics"
    subprocess.run(["g++","-std=c++11","-DARTOO_HOST_TEST","-funsigned-char","-O1","-g",
                    "-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer",
                    "-fno-pie","-no-pie","-I",str(root / "src"),str(fixture),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    print(f"All {len(cases)} generated images/glyphs: {total_pixels} RGB565 pixels match independent decoding under ASan/UBSan.")
