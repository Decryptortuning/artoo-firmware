#include "irqguard.h"
#include "../heapbounds.h"
#include <errno.h>

extern unsigned char __heap_start, __heap_end;

// Override libnosys' unbounded break with the linker-owned heap region. The
// reserved stack is a minimum margin; static/dynamic stack evidence is separate.
extern "C" void *_sbrk(ptrdiff_t increment)
{
    static unsigned char *position = &__heap_start;
    InterruptGuard guard;
    uintptr_t next = reinterpret_cast<uintptr_t>(position);
    if (!HeapBounds::advance(reinterpret_cast<uintptr_t>(&__heap_start),
                             reinterpret_cast<uintptr_t>(&__heap_end), next, increment)) {
        errno = ENOMEM;
        return reinterpret_cast<void*>(-1);
    }
    void *previous = position;
    position = reinterpret_cast<unsigned char*>(next);
    return previous;
}
