#ifndef ARTOO_WATCHDOG_H
#define ARTOO_WATCHDOG_H
#include <stdint.h>
#ifndef ARTOO_EXPERIMENTAL_IWDG
#define ARTOO_EXPERIMENTAL_IWDG 0
#endif
class Watchdog {
public:
#if ARTOO_EXPERIMENTAL_IWDG
    static bool beginFlash();
    static uint32_t status();
    static void boot();
    static bool resumeRunning();
    static void allowStart();
    static void adcComplete();
    static void foreground(bool heartbeat,bool uartPending);
    static void state(bool running);
#else
    static bool beginFlash() { return true; }
    static uint32_t status() { return 0; }
    static void boot() {}
    static bool resumeRunning() { return false; }
    static void allowStart() {}
    static void adcComplete() {}
    static void foreground(bool,bool) {}
    static void state(bool) {}
#endif
};
#endif
