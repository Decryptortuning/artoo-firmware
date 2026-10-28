#ifndef PARAMVALIDATION_H
#define PARAMVALIDATION_H

#include <stdint.h>
#include <string.h>
#include <limits.h>

namespace ParamValidation {

inline bool calibration(unsigned low, unsigned trim, unsigned high, unsigned deadZone = 0) {
    return high < 4096 && low < trim && trim < high &&
        trim - low > deadZone && high - trim > deadZone;
}

inline unsigned defaultMapping(unsigned raw) {
    return raw < 4 ? 3 - raw : raw;
}

inline unsigned mapping(unsigned raw, unsigned configured) {
    return configured == 0xff ? defaultMapping(raw) : configured;
}

inline bool stickConfig(unsigned input, unsigned expo) {
    return (input < 6 || input == 0xff) && (expo <= 100 || expo == 0xff);
}

template<typename Config>
bool mappings(const Config *configs) {
    unsigned seen = 0;
    for (unsigned raw = 0; raw < 6; ++raw) {
        if (!stickConfig(configs[raw].input, configs[raw].expo)) return false;
        const unsigned mapped = mapping(raw, configs[raw].input);
        // ADC sampling remaps RC axes only; both gimbal inputs are direct.
        if ((raw < 4 && mapped >= 4) || (raw >= 4 && mapped != raw)) return false;
        const unsigned bit = 1u << mapped;
        if (seen & bit) return false;
        seen |= bit;
    }
    return true;
}

inline bool sweep(uint32_t low, uint32_t high) {
    // Camera converts seconds to milliseconds, then rounds by adding 500.
    return low > 0 && low <= high && high <= (UINT32_MAX - 500u) / 1000u;
}

inline bool cameraPreset(float angle) {
    uint32_t bits;
    memcpy(&bits, &angle, sizeof bits);
    // Integer classification remains valid with the firmware's -ffast-math.
    return (bits & 0x7f800000u) != 0x7f800000u && angle >= 0.0f && angle <= 90.0f;
}
}
#endif
