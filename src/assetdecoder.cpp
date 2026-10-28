#include "assetdecoder.h"
#include "tampconfig.h"
#include "../vendor/tamp/decompressor.h"
#include "rledecoder.h"

#ifdef ARTOO_HOST_TEST
bool artooTestInInterrupt();
#endif
namespace {
struct Workspace {
    TampDecompressor decoder;
    uint8_t history[256];
    uint8_t output[32];
    bool busy;
};
#if UINTPTR_MAX == 0xffffffffu
static_assert(sizeof(Workspace)<=320, "bounded MCU asset workspace");
#endif
Workspace workspace;
#ifdef ARTOO_HOST_TEST
unsigned chunkSize=32;
#endif
bool inInterrupt() {
#ifdef ARTOO_HOST_TEST
    return artooTestInInterrupt();
#else
    uint32_t ipsr;
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
#endif
}
struct Lease {
    Lease() { workspace.busy=true; }
    ~Lease() { workspace.busy=false; }
};
}
#ifdef ARTOO_HOST_TEST
void AssetDecoder::testChunkSize(unsigned bytes) { chunkSize=bytes>=1 && bytes<=32 ? bytes : 32; }
#endif
bool AssetDecoder::decode(unsigned format,const uint8_t *data,unsigned bytes,unsigned pixels,
                          unsigned maxIndex,Emit emit,void *context) {
    if (inInterrupt() || workspace.busy || !data || !emit || pixels>320u*240u ||
        maxIndex>255 || !bytes || bytes>pixels*2+5) return false;
    Lease lease;
    auto sink=[&](uint8_t index,unsigned count) { emit(context,index,count); };
    if (format==1) return RleDecoder::decode(data,bytes,pixels,maxIndex,sink);
    if (format!=2) return false;
    const unsigned codec=*data++; --bytes;
    if (codec==0) return RleDecoder::decode(data,bytes,pixels,maxIndex,sink);
    if ((codec!=1 && codec!=2) || bytes<4) return false;
    const unsigned decodedBytes=unsigned(data[0]) | (unsigned(data[1])<<8) | (unsigned(data[2])<<16);
    data+=3; bytes-=3;
    if ((codec==1 && decodedBytes!=pixels) || (codec==2 && (!decodedBytes || decodedBytes>pixels*2+1))) return false;
    // Only independent extended W8 streams, literal width5..8, default dictionary,
    // no append/reset/extra header. Never permit a stream to resize static history.
    if ((data[0]&0xe7)!=0x02) return false;
    if (tamp_decompressor_init(&workspace.decoder,NULL,workspace.history,8)!=TAMP_OK) return false;
    RleDecoder::Stream rle(pixels,maxIndex);
    unsigned consumed=0,produced=0;
    for (unsigned steps=0;steps<=bytes+decodedBytes+1;++steps) {
        size_t used=0,written=0;
#ifdef ARTOO_HOST_TEST
        const unsigned room=chunkSize;
#else
        const unsigned room=sizeof workspace.output;
#endif
        const tamp_res result=tamp_decompressor_decompress(&workspace.decoder,workspace.output,room,&written,
                                                         data+consumed,bytes-consumed,&used);
        if (result<0 || used>bytes-consumed || written>decodedBytes-produced) return false;
        consumed+=used; produced+=written;
        for (unsigned i=0;i<written;++i) {
            const uint8_t value=workspace.output[i];
            if (codec==1) { if (value>maxIndex) return false; sink(value,1); }
            else if (!rle.put(value,sink)) return false;
        }
        if (!used && !written) {
            // Pinned decoder retains the unconsumed padding in its MSB-first
            // buffer. Canonical no-FLUSH streams end with <8 zero padding bits.
            const TampDecompressor &d=workspace.decoder;
            return consumed==bytes && produced==decodedBytes && d.configured &&
                !d.token_state && !d.skip_bytes && !d.last_was_flush &&
                d.bit_buffer_pos<8 && d.bit_buffer==0 && (codec==1 || rle.finish());
        }
    }
    return false;
}
