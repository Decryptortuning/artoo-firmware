#include "diagnostics.h"
#include "diagnosticrecord.h"
#include "stm32/rcc.h"
#include "stm32/systime.h"
#include "stm32/irqguard.h"
#include <string.h>

extern unsigned char __heap_start, __heap_end, _stack;
extern "C" {
volatile uint32_t artoo_fault_active __attribute__((section(".noinit")));
volatile uint32_t artoo_current_task __attribute__((section(".noinit")));
volatile uint32_t artoo_fault_record[16] __attribute__((section(".noinit")));
// Fault handlers switch to this separate aligned stack before entering C.
unsigned char artoo_fault_stack[512] __attribute__((section(".noinit.faultstack"), aligned(8), used));
}

extern "C" __attribute__((noinline, noreturn)) void artoo_fault_halt()
{
    for (;;) __asm__ volatile("nop");
}

extern "C" void artoo_capture_failure(uint32_t stage)
{
    __asm__ volatile("cpsid i" ::: "memory");
    volatile uint32_t *record = artoo_fault_record;
    record[0] = 0;
    record[1] = DiagnosticRecord::Format;
    for (unsigned i = 2; i < 12; ++i) record[i] = 0;
    record[12] = artoo_current_task;
    record[13] = 2; // Software-detected startup failure; no exception frame.
    record[14] = stage;
    uint32_t hash = 2166136261u;
    for (unsigned i = 1; i < 15; ++i) {
        const uint32_t word = record[i];
        for (unsigned b = 0; b < 4; ++b) { hash ^= (word >> (8*b)) & 255u; hash *= 16777619u; }
    }
    record[15] = hash;
    __asm__ volatile("dmb" ::: "memory");
    record[0] = DiagnosticRecord::Magic;
    __asm__ volatile("dsb" ::: "memory");
    artoo_fault_halt();
}

extern "C" void artoo_capture_fault(uint32_t *sp, uint32_t excReturn, uint32_t ipsr)
{
    volatile uint32_t *record = artoo_fault_record;
    record[0] = 0; // Invalidate first; validity is committed last.
    record[1] = DiagnosticRecord::Format;
    record[2] = ipsr;
    record[3] = excReturn;
    record[4] = reinterpret_cast<uintptr_t>(sp);
    const uint32_t cfsr = *reinterpret_cast<volatile uint32_t*>(0xe000ed28u);
    const bool validStack = DiagnosticRecord::stackValid(record[4], excReturn, cfsr);
    record[5] = validStack ? sp[6] : 0;
    record[6] = validStack ? sp[5] : 0;
    record[7] = validStack ? sp[7] : 0;
    record[8] = cfsr;
    record[9] = *reinterpret_cast<volatile uint32_t*>(0xe000ed2cu);
    record[10] = (cfsr & 0x80u) ? *reinterpret_cast<volatile uint32_t*>(0xe000ed34u) : 0;
    record[11] = (cfsr & 0x8000u) ? *reinterpret_cast<volatile uint32_t*>(0xe000ed38u) : 0;
    record[12] = artoo_current_task;
    record[13] = validStack ? 1 : 0;
    record[14] = 0;
    uint32_t hash = 2166136261u;
    for (unsigned i = 1; i < 15; ++i) {
        const uint32_t word = record[i];
        for (unsigned b = 0; b < 4; ++b) { hash ^= (word >> (8*b)) & 255u; hash *= 16777619u; }
    }
    record[15] = hash;
    __asm__ volatile("dmb" ::: "memory");
    record[0] = DiagnosticRecord::Magic;
    __asm__ volatile("dsb" ::: "memory");
    artoo_fault_halt(); // Capture only: no reset, UART or output changes.
}

namespace {
#if ARTOO_BENCH_NO_IDLE
volatile uint32_t metrics[Diagnostics::MetricWords];
SysTime::Ticks taskStart, lastLoop;
const uint32_t StackPaint = 0xa55ac33cu;
uint32_t elapsed(SysTime::Ticks now, SysTime::Ticks then) {
    const SysTime::Ticks delta = now - then;
    return delta > 0xffffffffu ? 0xffffffffu : static_cast<uint32_t>(delta);
}
void maximum(volatile uint32_t &value, uint32_t candidate) { if (candidate > value) value = candidate; }
#endif
}

void Diagnostics::init()
{
#if ARTOO_BENCH_NO_IDLE
    uintptr_t sp;
    __asm__ volatile("mrs %0, msp" : "=r"(sp));
    // Leave current frame and a guard untouched. Never paint heap/noinit or active stack.
    for (volatile uint32_t *p = reinterpret_cast<volatile uint32_t*>(&__heap_end);
         reinterpret_cast<uintptr_t>(p) + 64u < sp; ++p) *p = StackPaint;
#endif
}
void Diagnostics::taskBegin(unsigned id)
{
    artoo_current_task = id;
#if ARTOO_BENCH_NO_IDLE
    taskStart = SysTime::now();
    if (id < 7) ++metrics[17+id];
#endif
}
void Diagnostics::taskEnd(unsigned id)
{
#if ARTOO_BENCH_NO_IDLE
    if (id < 7) maximum(metrics[24+id], elapsed(SysTime::now(), taskStart));
#else
    (void)id;
#endif
    artoo_current_task = 0xffffffffu;
}
void Diagnostics::loop()
{
#if ARTOO_BENCH_NO_IDLE
    const SysTime::Ticks now = SysTime::now();
    if (lastLoop) maximum(metrics[16], elapsed(now, lastLoop));
    lastLoop = now;
#endif
}
void Diagnostics::heartbeatPosted()
{
#if ARTOO_BENCH_NO_IDLE
    ++metrics[13]; // Only SysTick writes this word.
#endif
}
void Diagnostics::heartbeatServed()
{
#if ARTOO_BENCH_NO_IDLE
    InterruptGuard guard;
    const uint32_t outstanding = metrics[13] - metrics[14];
    if (outstanding > 1) metrics[15] += outstanding - 1;
    metrics[14] = metrics[13]; // Number of posted heartbeats accounted for, not task count.
#endif
}
void Diagnostics::heap(uintptr_t position, bool failed)
{
#if ARTOO_BENCH_NO_IDLE
    // _sbrk owns an InterruptGuard already.
    if (failed) ++metrics[12];
    else {
        metrics[10] = position - reinterpret_cast<uintptr_t>(&__heap_start);
        maximum(metrics[11], metrics[10]);
    }
#else
    (void)position; (void)failed;
#endif
}
void Diagnostics::snapshot(uint8_t *out, uint16_t token, const uint32_t *uartStats)
{
    uint32_t fault[16];
    for (unsigned i = 0; i < 16; ++i) fault[i] = artoo_fault_record[i];
    const bool faultValid = DiagnosticRecord::valid(fault);
    memset(out, 0, ReplyBytes);
    out[0] = 1;
    out[1] = (faultValid ? 1 : 0) | (ARTOO_BENCH_NO_IDLE ? 2 : 0);
    out[2] = ReplyBytes;
    out[4] = token & 255;
    out[5] = token >> 8;
    DiagnosticRecord::encode32(out+8, Rcc::csr());
    if (faultValid) for (unsigned i = 0; i < 16; ++i) DiagnosticRecord::encode32(out+12+4*i, fault[i]);
#if ARTOO_BENCH_NO_IDLE
    const volatile uint32_t *p = reinterpret_cast<volatile uint32_t*>(&__heap_end);
    while (p < reinterpret_cast<const volatile uint32_t*>(&_stack) && *p == StackPaint) ++p;
    metrics[9] = reinterpret_cast<uintptr_t>(&_stack) - reinterpret_cast<uintptr_t>(p);
    {
        InterruptGuard guard;
        for (unsigned i = 9; i < MetricWords; ++i) DiagnosticRecord::encode32(out+76+4*i, metrics[i]);
    }
#endif
    for (unsigned i = 0; i < 9; ++i) DiagnosticRecord::encode32(out+76+4*i, uartStats[i]);
}
