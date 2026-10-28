
#include "systime.h"
#include "sys.h"
#include "hw.h"
#include "gpio.h"
#include "irqguard.h"
#include "timecapture.h"

static const unsigned SYSTICK_HZ = (Sys::CPU_HZ / 8);
static const unsigned SYSTICK_IRQ_HZ = 50;
static const uint32_t SYSTICK_RELOAD = (SYSTICK_HZ / SYSTICK_IRQ_HZ) - 1;

/*
 * tickBase --
 *    Coarse timer, incremented once per rollover. Written only by the ISR. Read by anyone.
 *
 * lastTick --
 *    Last published value, protected together with tickBase for all callers.
 *
 * heartbeat --
 *    User provided callback for periodic task execution.
 */
static SysTime::Ticks tickBase, lastTick;
static SysTime::HeartbeatCallback heartbeatCb;

static void emptyHeartbeat() {}

void SysTime::init(HeartbeatCallback hbcb)
{
    /*
     * The Cortex-M3 has a 24-bit hardware cycle timer, with an
     * interrupt and programmable reload.
     *
     * To use this timer to create a global monotonic timebase, we set
     * it up to roll over at a convenient rate (50Hz).  Those
     * rollovers increment a global counter, which we then combine
     * with the current timer value in ticks().
     */

    tickBase = 0;
    lastTick = 0;
    heartbeatCb = (hbcb == NULL) ? emptyHeartbeat : hbcb;

    NVIC.SysTick_CS = 0;
    NVIC.SysTick_RELOAD = SYSTICK_RELOAD;
    NVIC.SysTick = 0;

    // Enable timer, enable interrupt
    // NOTE: NVIC.SysTick gets loaded with NVIC.SysTick_RELOAD when 'enable' is applied
    NVIC.SysTick_CS = 3;
}

IRQ_HANDLER ISR_SysTick()
{
    {
        InterruptGuard guard;
        tickBase += SysTime::hzTicks(SYSTICK_IRQ_HZ);
    }

    heartbeatCb();
}

SysTime::Ticks SysTime::now()
{
    struct Counter {
        uint32_t value() const { return NVIC.SysTick; }
        bool pending() const { return NVIC.interruptControlState & (1u << 26); }
    } counter;
    InterruptGuard guard;
    return TimeCapture::now(counter, tickBase, lastTick, SYSTICK_RELOAD,
                            hzTicks(SYSTICK_IRQ_HZ));
}
