#include "button.h"
#include "buttonmanager.h"
#include "board.h"

Button::Button(GPIOPin p, GPIOPin active, GPIOPin backlight, Io::ButtonID bid, Polarity pol) :
    pin(p),
    activePin(active),
    backlightPin(backlight),
    polarity(pol),
    btnid(bid),
    pressed(false), quarantined(false),
    pressTimestamp(0),
    releaseTimestamp(0),
    reportedEvents(0)
{
}

void Button::init(bool enableIsr)
{
    pin.setControl(GPIOPin::IN_PULL);
    if (polarity == ActiveLow) {
        pin.pullup();
    } else {
        pin.pulldown();
    }

    pressed = physicallyPressed();

    if (enableIsr) {
        pin.irqInit();
        pin.irqSetRisingEdge();
        pin.irqSetFallingEdge();
        pin.irqEnable();
    }

    if (activePin != LED_GPIO_NONE) {
        activePin.setControl(GPIOPin::OUT_2MHZ);
        activePin.setHigh();
    }

    if (backlightPin != LED_GPIO_NONE) {
        backlightPin.setControl(GPIOPin::OUT_2MHZ);
        backlightPin.setLow();
    }
}

bool Button::captureEdge(bool &value, SysTime::Ticks &timestamp)
{
    if (!pin.irqPending()) return false;
    pin.irqAcknowledge();
    value = physicallyPressed();
    timestamp = SysTime::now();
    return true;
}

void Button::resynchronize(SysTime::Ticks timestamp)
{
    pressed = physicallyPressed();
    pressTimestamp = releaseTimestamp = timestamp;
    reportedEvents = SuppressHold;
    quarantined = pressed;
}

void Button::handleEdge(bool value, SysTime::Ticks now)
{
    // Foreground owns gesture state. Preserve captured edge time even when the
    // task was delayed. After queue loss, require release before a new gesture.
    if (pressed == value) return;
    pressed = value;
    if (quarantined) {
        if (!pressed) { releaseTimestamp = now; quarantined = false; }
        return;
    }

    if (isPressed()) {

        reportedEvents = 0;
        pressTimestamp = now;
        ButtonManager::dispatchEvt(this, Press);

        if (now - releaseTimestamp < SysTime::msTicks(DoubleClickGapMillis)) {
            // XXX: dispatch dc on press or release?
            ButtonManager::dispatchEvt(this, DoubleClick);
        }

    } else {
        releaseTimestamp = now;

        ButtonManager::dispatchEvt(this, Release);
        if ((releaseTimestamp - pressTimestamp) / SysTime::msTicks(1) < ClickMillis) {
            ButtonManager::dispatchEvt(this, ClickRelease);
        } else if (((releaseTimestamp - pressTimestamp) / SysTime::msTicks(1) > HoldMillis)
                  && ((releaseTimestamp - pressTimestamp) / SysTime::msTicks(1) < LongHoldMillis)) {
            ButtonManager::dispatchEvt(this, HoldRelease);
        } else if ((releaseTimestamp - pressTimestamp) / SysTime::msTicks(1) > LongHoldMillis) {
            ButtonManager::dispatchEvt(this, LongHoldRelease);
        }
    }

}

SysTime::Ticks Button::pressDuration() const
{
    /*
     * Return the current press duration.
     */

    if (isPressed()) {
        return SysTime::now() - pressTimestamp;
    }

    return 0;
}

void Button::pollForHold()
{
    /*
     * Called periodically when a button is pressed,
     * in order to generate hold events.
     */

    if (holdSuppressed()) {
        return;
    }

    SysTime::Ticks duration = pressDuration();

    if (!shortHoldFlagSet()) {
        if (duration > SysTime::msTicks(ShortHoldMillis)) {
            reportedEvents |= ShortHoldReported;
            ButtonManager::dispatchEvt(this, ShortHold);
        }
    }

    if (!holdFlagSet()) {
        if (duration > SysTime::msTicks(HoldMillis)) {
            reportedEvents |= HoldReported;
            ButtonManager::dispatchEvt(this, Hold);
        }
    }

    if (!longHoldFlagSet()) {
        if (duration > SysTime::msTicks(LongHoldMillis)) {
            reportedEvents |= LongHoldReported;
            ButtonManager::dispatchEvt(this, LongHold);
        }
    }
}
