#ifndef ARTOO_STARTUP_FAILURE_H
#define ARTOO_STARTUP_FAILURE_H
#include <stdint.h>
namespace StartupFailure {
enum Stage {
    None = 0, HsiReady = 1, HsiSwitch = 2, HseReady = 3,
    PllReady = 4, PllSwitch = 5, AdcDma = 6,
    AdcResetCalibration = 7, AdcCalibration = 8,
    AdcConversion = 9, BatteryFirstSample = 10
};
// Iteration bound, NOT a wall-clock deadline. Deliberately generous for startup;
// physical oscillator/peripheral latency remains a separate acceptance check.
static const unsigned PollBudget = 1u << 20;
static const int64_t BatteryWaitNs = 5000000000LL;
#ifdef ARTOO_HOST_TEST
uint32_t readRegister(volatile uint32_t &reg);
#else
inline uint32_t readRegister(volatile uint32_t &reg) { return reg; }
#endif
inline bool waitMasked(volatile uint32_t &reg, uint32_t mask, uint32_t expected,
                       unsigned budget = PollBudget) {
    while (budget--) if ((readRegister(reg) & mask) == expected) return true;
    return false;
}
// Check success first, including an actual measurement arriving on the boundary.
inline bool batteryExpired(bool initialized, int64_t start, int64_t now) {
    return !initialized && now - start >= BatteryWaitNs;
}
}
#endif
