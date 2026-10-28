#ifndef TXSCHEDULE_H
#define TXSCHEDULE_H

// One foreground producer owns enable/start/disable. Completion may arrive in
// an ISR; it releases ownership without producing or retrying any packet.
class TxSchedule {
    bool enabled;
    bool busy;
public:
    TxSchedule() : enabled(false), busy(false) {}
    void enable() { enabled = true; }
    void disable() {
        enabled = false;
        __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    }
    bool ready() const { return enabled && !__atomic_load_n(&busy, __ATOMIC_ACQUIRE); }
    void start() { __atomic_store_n(&busy, true, __ATOMIC_RELEASE); }
    void complete() { __atomic_store_n(&busy, false, __ATOMIC_RELEASE); }
};
#endif
