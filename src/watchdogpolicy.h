#ifndef ARTOO_WATCHDOG_POLICY_H
#define ARTOO_WATCHDOG_POLICY_H
#include <stdint.h>
namespace WatchdogPolicy {
struct Progress {
    uint32_t adc;
    unsigned healthy;
    bool observe(uint32_t nextAdc,bool heartbeat,bool uartPending) {
        if (!heartbeat || uartPending || nextAdc==adc) return false;
        adc=nextAdc;
        if (healthy<150) ++healthy;
        return true;
    }
};
struct Retained { uint32_t magic, streak, running, check; };
inline uint32_t checksum(uint32_t streak,uint32_t running) { return 0x7157d06bu ^ streak ^ (running*0x9e3779b9u); }
inline bool valid(const Retained &r) { return r.magic==0x57444731u && r.streak<=2 && r.running<=1 && r.check==checksum(r.streak,r.running); }
inline Retained boot(const Retained &old,bool watchdog,bool por) {
    uint32_t streak=0,running=0;
    if (!por && watchdog) {
        // Unknown retention after watchdog reset quarantines, never resets budget.
        streak=valid(old) ? (old.streak<2 ? old.streak+1:2):2;
        running=valid(old) ? old.running:0;
    }
    return {0x57444731u,streak,running,checksum(streak,running)};
}
}
#endif
