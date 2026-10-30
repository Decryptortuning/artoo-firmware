#ifndef ARTOO_PARAM_JOURNAL_H
#define ARTOO_PARAM_JOURNAL_H
#include <stdint.h>
#include <string.h>

// Explicit little-endian storage ABI. One snapshot per page; never erase the
// newest valid page. The 172-byte application/host ABI is unchanged.
namespace ParamJournal {
static const uint32_t Base = 0x0803f000u;
static const unsigned PageBytes = 2048, PayloadBytes = 172, RecordBytes = 192;
static const uint32_t Magic = 0x314a534fu;
static const uint16_t Commit = 0xa55au;
inline uint16_t get16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t get32(const uint8_t *p) { return get16(p) | uint32_t(get16(p+2)) << 16; }
inline void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void put32(uint8_t *p, uint32_t v) { put16(p,v); put16(p+2,v>>16); }
inline uint32_t crc(const uint8_t *p) {
    uint32_t c = 0xffffffffu;
    for (unsigned i=0; i<188; ++i) {
        if (i>=12 && i<16) continue;
        c ^= p[i];
        for (unsigned b=0;b<8;++b) c = (c>>1) ^ (0xedb88320u & (0u-(c&1u)));
    }
    return ~c;
}
inline bool valid(const uint8_t *p) {
    return get32(p)==Magic && get16(p+4)==1 && get16(p+6)==PayloadBytes &&
        get16(p+188)==Commit && get16(p+190)==0xffff && get32(p+12)==crc(p);
}
inline void encode(uint8_t *p,const void *payload,uint32_t generation) {
    memset(p,0xff,RecordBytes); put32(p,Magic); put16(p+4,1);
    put16(p+6,PayloadBytes); put32(p+8,generation);
    memcpy(p+16,payload,PayloadBytes); put32(p+12,crc(p));
}
struct Selection { int page; uint32_t generation; bool ambiguous; };
inline Selection select(const uint8_t *a,const uint8_t *b) {
    bool av=valid(a),bv=valid(b);
    if (!av && !bv) return {-1,0,false};
    if (!av) return {1,get32(b+8),false};
    if (!bv) return {0,get32(a+8),false};
    uint32_t ga=get32(a+8),gb=get32(b+8),delta=ga-gb;
    if (!delta) return {0,ga,memcmp(a,b,RecordBytes)!=0};
    if (delta==0x80000000u) return {-1,0,true};
    return delta<0x80000000u ? Selection{0,ga,false} : Selection{1,gb,false};
}
// Legacy page127 is exactly the old raw object followed by erased space.
// Any journal metadata/commit bytes in the tail forbid raw reinterpretation.
// Legacy integrity cannot be retroactively established: its existing semantic
// validation remains mandatory. This import is only used with no valid journal.
inline bool legacy(const uint8_t *page) {
    if (get32(page)==Magic) return false;
    for (unsigned i=PayloadBytes;i<PageBytes;++i) if (page[i]!=0xff) return false;
    return true;
}
template<class Flash> void read(unsigned page,uint8_t *dst,unsigned bytes) {
    for (unsigned i=0;i<bytes;i+=2) put16(dst+i,Flash::readHalfWord(Base+page*PageBytes+i));
}
template<class Flash> bool load(void *payload) {
    uint8_t a[RecordBytes],b[RecordBytes]; read<Flash>(0,a,sizeof a); read<Flash>(1,b,sizeof b);
    Selection s=select(a,b);
    if (s.ambiguous) return false;
    if (s.page>=0) { memcpy(payload,(s.page ? b:a)+16,PayloadBytes); return true; }
    if (get32(b)==Magic) return false;
    for (unsigned i=PayloadBytes;i<PageBytes;i+=2)
        if (Flash::readHalfWord(Base+PageBytes+i)!=0xffff) return false;
    memcpy(payload,b,PayloadBytes); return true;
}
template<class Flash> bool save(const void *payload) {
    uint8_t a[RecordBytes],b[RecordBytes]; read<Flash>(0,a,sizeof a); read<Flash>(1,b,sizeof b);
    Selection s=select(a,b);
    if (s.ambiguous) return false;
    if (s.page<0) {
        // Preserve unknown/corrupt storage rather than silently initializing it.
        if (get32(b)==Magic) return false;
        for (unsigned i=PayloadBytes;i<PageBytes;i+=2)
            if (Flash::readHalfWord(Base+PageBytes+i)!=0xffff) return false;
    }
    unsigned target=s.page==0 ? 1:0;
    uint32_t address=Base+target*PageBytes;
    encode(a,payload,s.page<0 ? 0:s.generation+1);
    Flash::unlock(); Flash::clearStatus();
    bool ok=Flash::erasePage(address);
    if (ok) {
        Flash::beginProgramming();
        for (unsigned i=0;i<188 && ok;i+=2) ok=Flash::programHalfWord(get16(a+i),address+i);
        if (!Flash::endProgramming()) ok=false;
    }
    // Verify every body halfword before the independent final commit write.
    for (unsigned i=0;i<188 && ok;i+=2) ok=Flash::readHalfWord(address+i)==get16(a+i);
    if (ok) {
        Flash::beginProgramming();
        ok=Flash::programHalfWord(Commit,address+188);
        if (!Flash::endProgramming()) ok=false;
    }
    Flash::lock();
    if (!ok) return false;
    read<Flash>(target,b,sizeof b);
    put16(a+188,Commit);
    return valid(b) && memcmp(a,b,sizeof a)==0;
}
}
#endif
