#ifndef IRQGUARD_H
#define IRQGUARD_H

#include <stdint.h>

// Preserve a caller's mask, including when entered from an ISR/nested guard.
// The host harness supplies only the architecture primitives, not this policy.
#ifdef ARTOO_HOST_TEST
uint32_t artooTestDisableInterrupts();
void artooTestRestoreInterrupts(uint32_t mask);
#endif

class InterruptGuard {
    uint32_t saved;
public:
    InterruptGuard() : saved(0) {
#ifdef ARTOO_HOST_TEST
        saved = artooTestDisableInterrupts();
#else
        asm volatile ("mrs %0, primask\n\tcpsid i" : "=r"(saved) :: "memory");
#endif
    }
    ~InterruptGuard() {
#ifdef ARTOO_HOST_TEST
        artooTestRestoreInterrupts(saved);
#else
        asm volatile ("msr primask, %0" :: "r"(saved) : "memory");
#endif
    }
    InterruptGuard(const InterruptGuard &) = delete;
    InterruptGuard &operator=(const InterruptGuard &) = delete;
};

#endif
