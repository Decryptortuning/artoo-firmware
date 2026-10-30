#include "src/paramjournal.h"
#include "src/watchdogpolicy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct Cut {};
struct Flash {
    static uint8_t memory[4096];
    static int cut,steps;
    static bool locked;
    static void point() { if (steps++==cut) throw Cut(); }
    static unsigned offset(uint32_t address) { assert(address>=ParamJournal::Base && address<ParamJournal::Base+4096); return address-ParamJournal::Base; }
    static void unlock() { locked=false; }
    static void lock() { locked=true; }
    static void clearStatus() {}
    static bool erasePage(uint32_t address) {
        unsigned off=offset(address); assert(off==0 || off==2048); assert(!locked);
        for(unsigned i=0;i<2048;i+=2) { point(); memory[off+i]=memory[off+i+1]=0xff; }
        return true;
    }
    static void beginProgramming() { assert(!locked); }
    static bool endProgramming() { return true; }
    static bool programHalfWord(uint16_t value,uint32_t address) {
        unsigned off=offset(address); assert(!locked && !(off&1)); point();
        assert((ParamJournal::get16(memory+off)&value)==value);
        ParamJournal::put16(memory+off,value); return true;
    }
    static uint16_t readHalfWord(uint32_t address) { return ParamJournal::get16(memory+offset(address)); }
};
uint8_t Flash::memory[4096]; int Flash::cut,Flash::steps; bool Flash::locked;
static void transactionCuts(const uint8_t *initial,const uint8_t *old,const uint8_t *next) {
    for(int cut=0;cut<=1120;++cut) {
        memcpy(Flash::memory,initial,4096); Flash::cut=cut;Flash::steps=0;Flash::locked=true;
        try { ParamJournal::save<Flash>(next); } catch(Cut &) {}
        // Simulated reboot: do not complete the interrupted transaction.
        uint8_t loaded[172]; assert(ParamJournal::load<Flash>(loaded));
        assert(!memcmp(loaded,old,172) || !memcmp(loaded,next,172));
    }
}
int main() {
    uint8_t initial[4096],a[172],b[172],c[172],loaded[172];
    for(unsigned i=0;i<172;++i) { a[i]=i; b[i]=i^0x5a; c[i]=i^0xa5; }
    // Legacy page126 may contain old application bytes; page127 raw API incl
    // zero rate-dial trim and erased sentinels is preserved byte for byte.
    a[32]=a[33]=0; a[80]=a[81]=0xff;
    memset(initial,0x42,2048); memset(initial+2048,0xff,2048);memcpy(initial+2048,a,172);
    transactionCuts(initial,a,b);
    memcpy(Flash::memory,initial,4096);Flash::cut=-1;Flash::steps=0;Flash::locked=true;
    assert(ParamJournal::save<Flash>(b));assert(Flash::locked);
    assert(!memcmp(Flash::memory+2048,initial+2048,2048));
    memcpy(initial,Flash::memory,4096);transactionCuts(initial,b,c);
    memcpy(Flash::memory,initial,4096);Flash::cut=-1;Flash::steps=0;
    assert(ParamJournal::save<Flash>(c));assert(ParamJournal::load<Flash>(loaded));assert(!memcmp(loaded,c,172));
    // Torn final commit, checksum corruption, unknown schema and ties.
    uint8_t p[192],q[192];ParamJournal::encode(p,a,0xffffffffu);ParamJournal::put16(p+188,ParamJournal::Commit);
    ParamJournal::encode(q,b,0);ParamJournal::put16(q+188,ParamJournal::Commit);
    assert(ParamJournal::select(p,q).page==1);
    q[40]^=1;assert(ParamJournal::select(p,q).page==0);
    ParamJournal::encode(q,b,0xffffffffu);ParamJournal::put16(q+188,ParamJournal::Commit);
    assert(ParamJournal::select(p,q).ambiguous);
    memcpy(q,p,sizeof p);assert(!ParamJournal::select(p,q).ambiguous);
    q[188]=0xff;assert(!ParamJournal::valid(q));
    memset(Flash::memory,0xff,4096);memcpy(Flash::memory,p,192);memcpy(Flash::memory+2048,q,192);
    Flash::memory[40]^=1;
    assert(!ParamJournal::load<Flash>(loaded));assert(!ParamJournal::save<Flash>(a));
    using namespace WatchdogPolicy;
    Progress progress={0,0};
    assert(!progress.observe(1,false,false));assert(!progress.observe(1,true,true));
    assert(progress.observe(1,true,false));assert(!progress.observe(1,true,false));
    for(unsigned i=2;i<=150;++i) assert(progress.observe(i,true,false));
    assert(progress.healthy==150);progress.adc=0xffffffffu;assert(progress.observe(0,true,false));
    Retained old={0,0,0,0};auto r=boot(old,true,false);assert(r.streak==2 && !r.running);
    r=boot(old,false,true);assert(valid(r)&&r.streak==0);
    r.running=1;r.check=checksum(r.streak,r.running);
    r=boot(r,true,false);assert(r.streak==1&&r.running);
    r=boot(r,true,false);assert(r.streak==2);r=boot(r,true,false);assert(r.streak==2);
    r.check^=1;assert(boot(r,true,false).streak==2);assert(boot(r,true,true).streak==0);
    puts("2242 interruption boundaries, migration/rollover/CRC/ties/wrap and watchdog progress/quarantine passed");
}
