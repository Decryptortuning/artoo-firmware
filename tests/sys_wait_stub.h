#ifndef ARTOO_TEST_SYS_WAIT_STUB_H
#define ARTOO_TEST_SYS_WAIT_STUB_H
// Permit compilation of the real LCD driver on host. Tests never call its
// peripheral init or waits; linker garbage collection excludes those paths.
#define SYS_H
class Sys {
public:
    static const unsigned CPU_HZ = 72000000;
    static void waitForInterrupt() {}
    static void waitForEvent() {}
};
#endif
