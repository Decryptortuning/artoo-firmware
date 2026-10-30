#include "watchdog.h"
#if ARTOO_EXPERIMENTAL_IWDG
#include "watchdogpolicy.h"
#include "stm32/hw.h"
#include "stm32/rcc.h"
#include "stm32/irqguard.h"
#include "diagnostics.h"
#include "startupfailure.h"

static volatile WatchdogPolicy::Retained retained __attribute__((section(".noinit")));
static volatile uint32_t adcEpoch;
static WatchdogPolicy::Progress progress;
static bool allowed,started,recoverRunning;
static unsigned stable;
#ifdef ARTOO_HOST_TEST
namespace WatchdogTest {
extern volatile uint32_t registers[4];
void write(unsigned index,uint32_t value);
}
static void writeIwdg(unsigned index,uint32_t value) { WatchdogTest::write(index,value); }
#else
static void writeIwdg(unsigned index,uint32_t value) {
    reinterpret_cast<volatile uint32_t*>(0x40003000u)[index]=value;
}
#endif
static void persist(uint32_t streak,uint32_t running)
{
    retained.magic=0;
    retained.streak=streak; retained.running=running;
    retained.check=WatchdogPolicy::checksum(streak,running);
#ifndef ARTOO_HOST_TEST
    asm volatile("dmb" ::: "memory");
#endif
    retained.magic=0x57444731u;
}
void Watchdog::boot()
{
    const WatchdogPolicy::Retained old={retained.magic,retained.streak,retained.running,retained.check};
    adcEpoch=0; progress={0,0}; allowed=false; started=false; stable=0;
    const uint32_t csr=Rcc::csr();
    const auto next=WatchdogPolicy::boot(old,csr&(Rcc::CSR_IWDG_RSTF|Rcc::CSR_WWDG_RSTF),csr&Rcc::CSR_POR_RSTF);
    persist(next.streak,next.running);
    recoverRunning=next.running!=0;
}
uint32_t Watchdog::status() { return 1u | (started ? 2u:0u) | (retained.streak>=2 ? 4u:0u) | (retained.streak<<8); }
bool Watchdog::resumeRunning() { return recoverRunning; }
void Watchdog::state(bool running) { persist(retained.streak,running); }
void Watchdog::allowStart() { allowed=true; }
void Watchdog::adcComplete() { ++adcEpoch; }
void Watchdog::foreground(bool heartbeat,bool uartPending)
{
    if (!allowed || retained.streak>=2) return; // Quarantine: no new watchdog activation.
    uint32_t epoch;
    { InterruptGuard guard; epoch=adcEpoch; }
    if (!progress.observe(epoch,heartbeat,uartPending)) return;
#ifdef ARTOO_HOST_TEST
    volatile uint32_t *iwdg=WatchdogTest::registers;
#else
    volatile uint32_t *iwdg=reinterpret_cast<volatile uint32_t*>(0x40003000u);
#endif
    if (!started && progress.healthy==150) {
        // Software mode only: hardware-start option mode needs separate review.
        if (!(FLASH.OBR & 4u)) artoo_capture_failure(StartupFailure::WatchdogSetup);
        RCC.CSR |= Rcc::CSR_LSION;
        if (!StartupFailure::waitMasked(RCC.CSR,Rcc::CSR_LSI_RDY,Rcc::CSR_LSI_RDY))
            artoo_capture_failure(StartupFailure::WatchdogSetup);
        writeIwdg(0,0x5555); writeIwdg(1,6); writeIwdg(2,4095);
        if (!StartupFailure::waitMasked(iwdg[3],3,0)) artoo_capture_failure(StartupFailure::WatchdogSetup);
        writeIwdg(0,0xaaaa); writeIwdg(0,0xcccc); started=true;
    }
    if (started) {
        writeIwdg(0,0xaaaa);
        // One minute of successful50Hz foreground/ADC progress clears streak.
        if (stable<3000 && ++stable==3000) persist(0,retained.running);
    }
}
#endif
