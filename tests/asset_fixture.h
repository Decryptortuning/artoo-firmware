#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include <algorithm>
#include "assetdecoder.h"
#include "lcd_sink.h"
ILI9341Parallel ILI9341Parallel::lcd;
std::vector<Gfx::Rect> ILI9341Parallel::regions;
std::vector<uint16_t> ILI9341Parallel::pixels;
static bool interruptContext=false;
bool artooTestInInterrupt() { return interruptContext; }
static std::vector<uint8_t> golden;
struct Output {
    const Gfx::ImagePalette *palette;
    unsigned count,limit,offset;
    bool compare;
};
static void emit(void *opaque,uint8_t index,unsigned repeat) {
    Output &out=*static_cast<Output*>(opaque);
    assert(index<=out.palette->maxIdx && repeat<=out.limit-out.count);
    while (repeat--) {
        if (out.compare) {
            const unsigned pos=out.offset+out.count*2;
            assert(pos+1<golden.size());
            const uint16_t expected=golden[pos] | (unsigned(golden[pos+1])<<8);
            assert(out.palette->colors[index]==expected);
        }
        ++out.count;
    }
}
static bool tryDecode(const std::vector<uint8_t>&bytes,unsigned pixels,const Gfx::ImagePalette *p) {
    Output out={p,0,pixels,0,false};
    const bool ok=AssetDecoder::decode(2,bytes.data(),bytes.size(),pixels,p->maxIdx,emit,&out);
    assert(out.count<=pixels);
    if (ok) assert(out.count==pixels);
    return ok;
}
static void check(const void *data,unsigned len,unsigned count,const Gfx::ImagePalette *p,unsigned offset) {
    const uint8_t *bytes=static_cast<const uint8_t*>(data);
    for (unsigned chunk : {1u,7u,32u}) {
        AssetDecoder::testChunkSize(chunk);
        Output out={p,0,count,offset,true};
        assert(AssetDecoder::decode(2,bytes,len,count,p->maxIdx,emit,&out));
        assert(out.count==count);
    }
    AssetDecoder::testChunkSize(32);
    // All prefixes for small streams; representative cuts across large assets.
    for (unsigned cut=0;cut<len;cut+=(len<64?1:std::max(1u,len/32))) {
        std::vector<uint8_t> truncated(bytes,bytes+cut);
        assert(!tryDecode(truncated,count,p));
    }
    std::vector<uint8_t> altered(bytes,bytes+len);
    altered.push_back(0); assert(!tryDecode(altered,count,p));
    if (bytes[0]) {
        assert(len>=5);
        for (unsigned invalid : {0x20u,0x04u,0x01u,0x02u}) {
            altered.assign(bytes,bytes+len); altered[4]^=invalid;
            assert(!tryDecode(altered,count,p));
        }
        altered.assign(bytes,bytes+len); altered[1]=altered[2]=altered[3]=0xff;
        assert(!tryDecode(altered,count,p));
    }
    // Bit corruption is not an integrity checksum: some changed images remain
    // valid, but every admitted prefix is still palette/pixel/input bounded.
    for (unsigned i=0;i<std::min(len,32u);++i) {
        altered.assign(bytes,bytes+len); altered[i]^=0xff;
        tryDecode(altered,count,p);
    }
    Output out={p,0,count,offset,true};
    interruptContext=true;
    assert(!AssetDecoder::decode(2,bytes,len,count,p->maxIdx,emit,&out));
    interruptContext=false;
    assert(out.count==0);
}
static void recursiveEmit(void *opaque,uint8_t index,unsigned count) {
    const uint8_t raw[]={0,0,0,0x80};
    Output &out=*static_cast<Output*>(opaque);
    assert(!AssetDecoder::decode(2,raw,sizeof raw,1,0,emit,opaque));
    assert(index==0);out.count+=count;
}
static void adversarial() {
    const uint16_t colors[]={0,1,2,3,4};
    const Gfx::ImagePalette p={colors,4};
    const uint8_t raw[]={0,0,0,0x80};
    Output out={&p,0,1,0,false};
    assert(AssetDecoder::decode(2,raw,sizeof raw,1,4,recursiveEmit,&out) && out.count==1);
    uint32_t random=42;
    std::vector<uint8_t> bytes(64);
    for (unsigned trial=0;trial<100000;++trial) {
        bytes.resize(trial%65);
        for (auto &byte:bytes) {random=random*1664525u+1013904223u;byte=random>>24;}
        tryDecode(bytes,32,&p);
    }
    assert(!AssetDecoder::decode(2,raw,sizeof raw,76801,4,emit,&out));
}

static void clearSink() { ILI9341Parallel::regions.clear(); ILI9341Parallel::pixels.clear(); }
static void checkSink(unsigned count,unsigned offset,bool tinted=false) {
    assert(ILI9341Parallel::pixels.size()==count);
    Gfx::FontColorConverter converter;
    converter.init(0x1357,0xf81f); // same documented background/foreground contract
    for (unsigned i=0;i<count;++i) {
        const unsigned pos=offset+i*2;
        uint16_t color=golden[pos] | (unsigned(golden[pos+1])<<8);
        if (tinted) color=converter.grayToColor(color);
        assert(ILI9341Parallel::pixels[i]==color);
    }
}
