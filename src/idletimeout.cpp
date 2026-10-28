#include "idletimeout.h"
#include "powermanager.h"
#include "ui.h"
#include "buttonmanager.h"
#include "haptic.h"

// Explicit development build only. All other builds retain the normal timeout.
#ifndef ARTOO_BENCH_NO_IDLE
#define ARTOO_BENCH_NO_IDLE 0
#endif
#if ARTOO_BENCH_NO_IDLE != 0 && ARTOO_BENCH_NO_IDLE != 1
#error "ARTOO_BENCH_NO_IDLE must be 0 or 1"
#endif
unsigned IdleTimeout::ticker;
bool IdleTimeout::userDisabled = ARTOO_BENCH_NO_IDLE;

void IdleTimeout::onButtonEvent(Button *b, Button::Event e)
{
    /*
     * Look for a button combo to disable idle timeout.
     */

    UNUSED(b);
    UNUSED(e);

    if (userDisabled) {
        return;
    }

    if (ButtonManager::button(Io::ButtonA).isHeldLong() &&
        ButtonManager::button(Io::ButtonLoiter).isHeldLong() &&
        ButtonManager::button(Io::ButtonCameraClick).isHeldLong())
    {
        userDisabled = true;
        Haptic::startPattern(Haptic::SingleMedium);
    }
}

bool IdleTimeout::enabled()
{
    /*
     * states in which we should disable countdown;
     */

    if (userDisabled) {
        return false;
    }

    if (Ui::instance.state() == Ui::Updater) {
        return false;
    }

    if (FlightManager::instance.linkIsConnected()) {
        return false;
    }

    return true;
}

void IdleTimeout::tick()
{
    if (!enabled()) {
        reset();
        return;
    }

    switch (++ticker) {
    case IDLE_WARN:
        Ui::instance.pendEvent(Event::SystemIdleWarning);
        break;

    case IDLE_TIMEOUT:
        PowerManager::shutdown();
        break;
    }
}

void IdleTimeout::reset()
{
    if (Ui::instance.alertManager.currentEvent() == Event::SystemIdleWarning) {
        Ui::instance.alertManager.dismiss();
    }

    ticker = 0;
}
