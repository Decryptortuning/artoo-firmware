#ifndef HEAPBOUNDS_H
#define HEAPBOUNDS_H
#include <stdint.h>
#include <stddef.h>
namespace HeapBounds {
inline bool advance(uintptr_t start, uintptr_t end, uintptr_t &current, ptrdiff_t increment) {
    if (current < start || current > end) return false;
    if (increment >= 0) {
        if (uintptr_t(increment) > end - current) return false;
        current += uintptr_t(increment);
    } else {
        const uintptr_t decrease = uintptr_t(-(increment + 1)) + 1;
        if (decrease > current - start) return false;
        current -= decrease;
    }
    return true;
}
}
#endif
