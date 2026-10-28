#ifndef TIMECAPTURE_H
#define TIMECAPTURE_H
#include <stdint.h>

// Called with interrupts masked; the adapter exposes only the hardware reads.
// Pending ticks coalesce in Cortex-M3 hardware. This clock cannot recover time
// lost while interrupts are masked for multiple periods; it clamps publication
// instead of fabricating additional elapsed periods or moving backwards.
namespace TimeCapture {
template<typename Counter>
int64_t now(Counter &counter, int64_t coarse, int64_t &last, uint32_t reload,
            int64_t period) {
    const uint32_t before = counter.value();
    const bool pending = counter.pending();
    const uint32_t after = counter.value();
    const bool wrapped = after > before;
    const uint32_t fraction = reload - after;
    int64_t captured = coarse + ((pending || wrapped) ? period : 0);
    const uint64_t factor = uint64_t(period) * 0x100000000ULL / (reload + 1u);
    captured += (fraction * factor) >> 32;
    if (captured < last) captured = last;
    last = captured;
    return captured;
}
}
#endif
