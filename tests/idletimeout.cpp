// Actual IdleTimeout implementation; UI/flight/button observations and shutdown
// are inert sinks. This does not simulate PowerManager or physical shutdown.
#include <assert.h>
#include <stdio.h>
#include "src/idletimeout.h"
#include "src/ui_events.h"
#include "src/haptic.h"

static unsigned shutdowns, warnings, haptics;
#define POWER_MANAGER_H
class PowerManager {
public:
    static void shutdown() { ++shutdowns; }
};
#define UI_H
class Ui {
public:
    enum State { Telem, Updater };
    static Ui instance;
    State mode = Telem;
    State state() { return mode; }
    struct Alerts {
        Event::ID event = Event::None;
        Event::ID currentEvent() { return event; }
        void dismiss() { event = Event::None; }
    } alertManager;
    void pendEvent(Event::ID event) {
        assert(event == Event::SystemIdleWarning);
        ++warnings; alertManager.event = event;
    }
};
Ui Ui::instance;
#define BUTTON_MANAGER_H
class ButtonManager {
public:
    struct Held { bool held = false; bool isHeldLong() const { return held; } };
    static Held buttons[9];
    static Held &button(Io::ButtonID id) { return buttons[id]; }
};
ButtonManager::Held ButtonManager::buttons[9];
class FlightManager {
public:
    static FlightManager instance;
    bool connected = false;
    bool linkIsConnected() { return connected; }
};
FlightManager FlightManager::instance;
void Haptic::startPattern(Pattern p) { assert(p == SingleMedium); ++haptics; }
#include "src/idletimeout.cpp"

static void tick(unsigned n) { while (n--) IdleTimeout::tick(); }
int main()
{
    constexpr unsigned warningTick = 50 * 60 * 10;
    constexpr unsigned shutdownTick = warningTick + 50 * 7;
#if ARTOO_BENCH_NO_IDLE
    // No combo, updater state or connected vehicle is needed to hold idle off.
    tick(shutdownTick * 4);
    assert(warnings == 0 && shutdowns == 0 && haptics == 0);
    IdleTimeout::reset(); tick(shutdownTick * 2);
    IdleTimeout::onButtonEvent(nullptr, Button::Press);
    assert(warnings == 0 && shutdowns == 0 && haptics == 0);
    assert(Ui::instance.state() == Ui::Telem && !FlightManager::instance.connected);
#else
    tick(warningTick - 1); assert(warnings == 0 && shutdowns == 0);
    tick(1); assert(warnings == 1 && shutdowns == 0);
    tick(349); assert(shutdowns == 0);
    tick(1); assert(shutdowns == 1);
    IdleTimeout::reset();
    assert(Ui::instance.alertManager.event == Event::None);
    // Reset starts a fresh full countdown; an active connection still pauses it.
    tick(warningTick - 1);
    FlightManager::instance.connected = true; tick(shutdownTick * 2);
    assert(warnings == 1 && shutdowns == 1);
    FlightManager::instance.connected = false;
    tick(warningTick - 1); assert(warnings == 1);
    tick(1); assert(warnings == 2 && shutdowns == 1);
    Ui::instance.mode = Ui::Updater; tick(shutdownTick * 2);
    assert(warnings == 2 && shutdowns == 1);
    Ui::instance.mode = Ui::Telem;
    tick(shutdownTick); assert(warnings == 3 && shutdowns == 2);
    IdleTimeout::reset();
    // Existing manual disable combo remains available on normal builds.
    ButtonManager::buttons[Io::ButtonA].held = true;
    ButtonManager::buttons[Io::ButtonLoiter].held = true;
    IdleTimeout::onButtonEvent(nullptr, Button::Press); assert(haptics == 0);
    ButtonManager::buttons[Io::ButtonCameraClick].held = true;
    IdleTimeout::onButtonEvent(nullptr, Button::Press); assert(haptics == 1);
    tick(shutdownTick * 2); assert(warnings == 3 && shutdowns == 2);
#endif
    puts("Actual IdleTimeout: default countdown/reset and explicit bench idle-only suppression passed.");
}
