#ifndef DIAGNOSTICRECORD_H
#define DIAGNOSTICRECORD_H
#include <stdint.h>
#include <stddef.h>
namespace DiagnosticRecord {
static const uint32_t Magic = 0x41525446;
static const uint32_t Format = 0x00010040;
static const unsigned Words = 16;
inline uint32_t checksum(const uint32_t *words) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 1; i < 15; ++i)
        for (unsigned b = 0; b < 4; ++b) {
            hash ^= (words[i] >> (8*b)) & 255u;
            hash *= 16777619u;
        }
    return hash;
}
inline bool valid(const uint32_t *words) {
    const bool exception = words[14] == 0 && (words[13] & ~1u) == 0;
    const bool software = words[13] == 2 && words[14] >= 1 && words[14] <= 10;
    return words[0] == Magic && words[1] == Format && (exception || software) &&
        words[15] == checksum(words);
}
inline bool stackValid(uintptr_t sp, uint32_t excReturn, uint32_t cfsr) {
    // Basic Cortex-M3 frame; reject failed stacking/unstacking and invalid EXC_RETURN.
    return (excReturn == 0xfffffff1u || excReturn == 0xfffffff9u || excReturn == 0xfffffffdu) &&
        !(cfsr & 0x1818u) && !(sp & 3u) && sp >= 0x20000000u && sp <= 0x2000c000u - 32u;
}
inline void encode32(uint8_t *dst, uint32_t value) {
    for (unsigned b = 0; b < 4; ++b) dst[b] = static_cast<uint8_t>(value >> (8*b));
}
}
#endif
