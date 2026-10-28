#ifndef PARAMSTORE_H
#define PARAMSTORE_H

#include <stdint.h>
#include <string.h>

namespace ParamStore {
// Caller protects each operation with its interrupt guard. Completion of an
// older snapshot must not clear a newer edit, and failures remain pending.
struct PendingWrite {
    bool dirty;
    uint32_t generation;
    void mark() { dirty = true; ++generation; }
    void complete(uint32_t snapshotGeneration, bool success) {
        if (success && generation == snapshotGeneration) dirty = false;
    }
};

// One production transaction, with a narrow flash adapter for fault injection.
// Preserves the existing single-page wire/layout contract; not power atomic.
template<typename Flash>
bool write(uint32_t address, const void *record, unsigned bytes) {
    if (!bytes || bytes % 2) return false;
    Flash::unlock();
    Flash::clearStatus();
    bool ok = Flash::erasePage(address);
    if (ok) {
        Flash::beginProgramming();
        for (unsigned offset = 0; offset < bytes; offset += 2) {
            uint16_t value;
            memcpy(&value, static_cast<const uint8_t*>(record) + offset, sizeof value);
            if (!Flash::programHalfWord(value, address + offset)) {
                ok = false;
                break;
            }
        }
        if (!Flash::endProgramming()) ok = false;
    }
    Flash::lock();
    if (!ok) return false;
    for (unsigned offset = 0; offset < bytes; offset += 2) {
        uint16_t expected;
        memcpy(&expected, static_cast<const uint8_t*>(record) + offset, sizeof expected);
        if (Flash::readHalfWord(address + offset) != expected) return false;
    }
    return true;
}
}
#endif
