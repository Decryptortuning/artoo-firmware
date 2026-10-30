#include "src/watchdog.h"
#include "src/startupfailure.h"
#include "src/stm32/rcc.h"
#include <assert.h>
#include <vector>
#include <utility>
#include <stdio.h>
volatile RCC_t RCC;
volatile FLASH_t FLASH;
namespace WatchdogTest {
volatile uint32_t registers[4];
std::vector<std::pair<unsigned,uint32_t>> writes;
void write(unsigned index,uint32_t value) { registers[index]=value;writes.push_back({index,value}); }
}
uint32_t artooTestDisableInterrupts() { return 0; }
void artooTestRestoreInterrupts(uint32_t mask) { assert(mask==0); }
uint32_t StartupFailure::readRegister(volatile uint32_t &reg) {
    if (&reg==&RCC.CSR) reg|=2;
    return reg;
}
extern "C" void artoo_capture_failure(uint32_t stage) { throw stage; }
static void boot(uint32_t csr) {
    RCC.CSR=csr;Rcc::latchAndClearResetFlags();Watchdog::boot();
    WatchdogTest::writes.clear();WatchdogTest::registers[3]=0;
}
static void progress(unsigned count) {
    for(unsigned i=0;i<count;++i) { Watchdog::adcComplete();Watchdog::foreground(true,false); }
}
int main() {
    FLASH.OBR=4;boot(Rcc::CSR_POR_RSTF);progress(200);assert(WatchdogTest::writes.empty());
    Watchdog::allowStart();progress(149);assert(WatchdogTest::writes.empty());
    Watchdog::foreground(true,false);assert(WatchdogTest::writes.empty());
    Watchdog::adcComplete();Watchdog::foreground(true,true);assert(WatchdogTest::writes.empty());
    Watchdog::foreground(true,false);assert(WatchdogTest::writes.size()==6);
    assert(WatchdogTest::writes[0].second==0x5555 && WatchdogTest::writes[1].second==6);
    assert(WatchdogTest::writes[2].second==4095 && WatchdogTest::writes[4].second==0xcccc);
    WatchdogTest::writes.clear();Watchdog::foreground(true,false);assert(WatchdogTest::writes.empty());
    progress(1);assert(WatchdogTest::writes.size()==1 && WatchdogTest::writes[0].second==0xaaaa);
    assert(Watchdog::beginFlash());
    WatchdogTest::writes.clear();assert(!Watchdog::beginFlash());
    Watchdog::foreground(true,false);assert(!Watchdog::beginFlash());assert(WatchdogTest::writes.empty());
    progress(1);assert(Watchdog::beginFlash());assert(!Watchdog::beginFlash());
    Watchdog::state(true);boot(Rcc::CSR_PIN_RSTF|Rcc::CSR_IWDG_RSTF);
    assert(!Rcc::pinReset() && Watchdog::resumeRunning());Watchdog::allowStart();progress(150);assert(!WatchdogTest::writes.empty());
    boot(Rcc::CSR_IWDG_RSTF);assert(Watchdog::resumeRunning());Watchdog::allowStart();progress(4000);assert(WatchdogTest::writes.empty());assert(Watchdog::status()==0x205);
    boot(Rcc::CSR_POR_RSTF);assert(!Watchdog::resumeRunning());Watchdog::allowStart();FLASH.OBR=0;
    bool rejected=false;try { progress(150); } catch(uint32_t stage) { rejected=stage==13; }
    assert(rejected && WatchdogTest::writes.empty());
    puts("Actual watchdog driver: admission/progress/keys/divider/reset precedence/quarantine/hardware-mode rejection passed");
}
