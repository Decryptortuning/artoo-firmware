#include "buttonmanager.h"
#include "powermanager.h"
#include "flightmanager.h"
#include "cameracontrol.h"
#include "vehicleconnector.h"
#include "dsm.h"
#include "ui.h"
#include "tasks.h"
#include "idletimeout.h"
#include "manualoverride.h"
#include "board.h"

#include "stm32/sys.h"
#include "stm32/hwtimer.h"

uint16_t ButtonManager::buttonPressMask;
RingBuffer<32, ButtonManager::Edge> ButtonManager::edges;
bool ButtonManager::edgeOverflow;
uint32_t ButtonManager::droppedEdges;

// must match order of ButtonID
Button ButtonManager::buttons[] = {
    Button(BTN_PWR_GPIO,        LED_BLUE_S1_GPIO,   LED_WHITE_S1_GPIO, Io::ButtonPower, Button::ActiveLow),
    Button(BTN_FLY_GPIO,        LED_BLUE_S2_GPIO,   LED_WHITE_S2_GPIO, Io::ButtonFly),
    Button(BTN_RTL_GPIO,        LED_BLUE_S3_GPIO,   LED_WHITE_S3_GPIO, Io::ButtonRTL),
    Button(BTN_LOITER_GPIO,     LED_BLUE_S4_GPIO,   LED_WHITE_S4_GPIO, Io::ButtonLoiter),
    Button(BTN_A_GPIO,          LED_BLUE_S5_GPIO,   LED_WHITE_S5_GPIO, Io::ButtonA),
    Button(BTN_B_GPIO,          LED_BLUE_S6_GPIO,   LED_WHITE_S6_GPIO, Io::ButtonB),
    Button(BTN_PRESET1_GPIO,    LED_GPIO_NONE,      LED_GPIO_NONE,     Io::ButtonPreset1),
    Button(BTN_PRESET2_GPIO,    LED_GPIO_NONE,      LED_GPIO_NONE,     Io::ButtonPreset2),
    Button(BTN_CAM_CLICK_GPIO,  LED_GPIO_NONE,      LED_GPIO_NONE,     Io::ButtonCameraClick)
};

unsigned ButtonManager::eventIdx;
RingBuffer<8> ButtonManager::pendingEventIdxs;
ButtonManager::ButtonEvent ButtonManager::events[7];

void ButtonManager::init()
{
    ////////////////////////////////////
    // button gpio init

    buttons[Io::ButtonPower].init(true);
    buttons[Io::ButtonFly].init(true);
    buttons[Io::ButtonRTL].init(true);
    buttons[Io::ButtonLoiter].init(true);
    buttons[Io::ButtonA].init(true);
    buttons[Io::ButtonB].init(true);
    buttons[Io::ButtonPreset1].init(true);
    buttons[Io::ButtonPreset2].init(true);
    buttons[Io::ButtonCameraClick].init(true);
    buttonPressMask = 0;
    for (unsigned i = 0; i < arraysize(buttons); ++i)
        if (buttons[i].isPressed()) buttonPressMask |= 1u << i;


    ////////////////////////////////////
    // button backlight init

    HwTimer ledTimer(&LED_PWM_TIM);
    GPIOPin dimmer = LED_PWM_GPIO;

    const unsigned hz = 120;

    AFIO.MAPR |= (0x3 << 10);     // 'full remap' TIM3 to PC6-9

    ledTimer.init(Sys::CPU_HZ / 1000 / hz, 1000);
    ledTimer.configureChannelAsOutput(LED_PWM_CH,
                                       HwTimer::ActiveLow,
                                       HwTimer::Pwm1,
                                       HwTimer::SingleOutput);
    setBacklight(0);    // we'll enable this once the display backlight comes up
    ledTimer.enableChannel(LED_PWM_CH);

    // must configure output as open collector,
    // since the pullup will pull our hi-z state up to 5V
    // instead of 3.3V if we were to drive it directly.
    dimmer.setControl(GPIOPin::OUT_ALT_OPEN_50MHZ);

    ////////////////////////////////////
    // button functions

    for (unsigned i = 0; i < arraysize(Params::sys.storedValues.buttonConfigs); ++i){
        Io::ButtonID id = static_cast<Io::ButtonID>(Io::ButtonLoiter + i);
        ButtonFunction::Config & cfg = ButtonFunction::get(id);

        if (!Params::isInitialized(&cfg, sizeof cfg)) {
            cfg.buttonID = id;
            cfg.buttonEvt = Button::ClickRelease;
            cfg.shotID = ButtonFunction::ShotNone;
            strncpy(cfg.descriptor, "None", strlen("None") + 1);
        }
    }
}

bool ButtonManager::setButtonFunction(Io::ButtonID id, const ButtonFunction::Config *c)
{
    if (ButtonFunction::validId(id)) {
        Button & btn = button(id);
        ButtonFunction::Config & cfg = ButtonFunction::get(id);
        memcpy(&cfg, c, sizeof cfg);
        cfg.descriptor[sizeof cfg.descriptor - 1] = '\0';

        if (cfg.descriptor[0] == '\0') {
            // XXX: no descriptor indicates this button isn't currently mapped to a function
        }

        btn.setGreenLed(cfg.hilighted());

        return true;
    }

    return false;
}

void ButtonManager::setBacklight(unsigned percent)
{
    /*
     * set brightness level, 0-100.
     */

    ASSERT(percent <= 100);

    HwTimer ledTimer(&LED_PWM_TIM);
    uint16_t duty = percent / 100.0f * ledTimer.period();
    ledTimer.setDuty(LED_PWM_CH, duty);
}

void ButtonManager::isr(Io::ButtonID id)
{
    InterruptGuard guard; // serialize different EXTI producers
    Edge edge;
    edge.id = id;
    if (!buttons[id].captureEdge(edge.pressed, edge.time)) return;
    if (edgeOverflow || !edges.enqueue(edge)) {
        edgeOverflow = true;
        ++droppedEdges;
    }
    Tasks::trigger(Tasks::ButtonHold);
}

void ButtonManager::task()
{
    // Only foreground dispatches application callbacks or mutates gestures.
    // A lost edge invalidates the whole queued history. Reconcile physical
    // levels and suppress held gestures until release, rather than synthesizing
    // click/hold commands from an incomplete history.
    {
        InterruptGuard guard;
        if (edgeOverflow) {
            edges.init();
            edgeOverflow = false;
            buttonPressMask = 0;
            const SysTime::Ticks now = SysTime::now();
            for (unsigned i = 0; i < arraysize(buttons); ++i) {
                buttons[i].resynchronize(now);
                if (buttons[i].isPressed()) buttonPressMask |= 1u << i;
            }
        }
    }
    unsigned budget = 31;
    while (budget--) {
        Edge edge;
        {
            InterruptGuard guard;
            if (edgeOverflow || edges.empty()) break;
            edge = edges.dequeue();
        }
        if (edge.pressed) buttonPressMask |= 1u << edge.id;
        else buttonPressMask &= ~(1u << edge.id);
        buttons[edge.id].handleEdge(edge.pressed, edge.time);
    }
    if (!edges.empty() || edgeOverflow) {
        Tasks::trigger(Tasks::ButtonHold);
        return; // process captured edges before deriving any hold events
    }
    for (unsigned i = 0; i < arraysize(buttons); ++i) {
        if (buttonPressMask & (1u << i)) buttons[i].pollForHold();
    }
    // Held-button checks run on the existing 50 Hz heartbeat, avoiding a busy
    // task loop. Threshold constants and captured-edge release timing remain.
}

void ButtonManager::dispatchEvt(Button *b, Button::Event evt)
{
    /*
     * Send button events to interested parties.
     *
     * Some handlers indicate they've handled the event,
     * in which case we should stop propagating to subsequent handlers.
     */

    if (evt == Button::Press) {
        IdleTimeout::reset();
    }

    ManualOverride::onButtonEvent(b, evt);
    IdleTimeout::onButtonEvent(b, evt);

    if (Ui::instance.updater.onButtonEvent(b, evt)) {
        return;
    }

    if (Ui::instance.alertManager.onButtonEvent(b, evt)) {
        return;
    }

    switch (b->id()) {
    case Io::ButtonPower:
        FlightManager::instance.onPowerButtonEvt(b, evt);
        PowerManager::onButtonEvt(b, evt);
        break;

    case Io::ButtonFly:
        if (Ui::instance.state() == Ui::Arming) {
            Ui::instance.arming.onFlyButtonEvt(b, evt);
        }
        FlightManager::instance.onFlyButtonEvt(b, evt);
        break;

    case Io::ButtonRTL:
        ButtonFunction::onButtonExtEvent(b, evt);
        FlightManager::instance.onRtlButtonEvt(b, evt);
        break;

    case Io::ButtonLoiter:
        ButtonFunction::onButtonExtEvent(b, evt);
        FlightManager::instance.onPauseButtonEvt(b, evt);
        Dsm::instance.onLoiterButtonEvt(b, evt);
        ButtonFunction::onButtonEvent(b, evt);
        break;

    case Io::ButtonA:
        ButtonFunction::onButtonExtEvent(b, evt);
        FlightManager::instance.onAButtonEvt(b, evt);
        VehicleConnector::instance.onButtonEvent(b, evt);
        ButtonFunction::onButtonEvent(b, evt);
        break;

    case Io::ButtonB:
        ButtonFunction::onButtonExtEvent(b, evt);
        FlightManager::instance.onBButtonEvt(b, evt);
        VehicleConnector::instance.onButtonEvent(b, evt);
        ButtonFunction::onButtonEvent(b, evt);
        break;

    case Io::ButtonPreset1:
        ButtonFunction::onButtonExtEvent(b, evt);
        CameraControl::instance.onButtonEvt(b, evt);
        break;

    case Io::ButtonPreset2:
        ButtonFunction::onButtonExtEvent(b, evt);
        CameraControl::instance.onButtonEvt(b, evt);
        break;

    case Io::ButtonCameraClick:
        ButtonFunction::onButtonExtEvent(b, evt);
        CameraControl::instance.onButtonEvt(b, evt);
        break;
    }

    if (ManualOverride::isEnabled()) {
        // suppress button events such that when we're overriding
        // into manual mode, it's not possible for sololink
        // to treat inadvertent button presses as mode change commands
        return;
    }

    if ( evt == Button::Release || (evt == Button::DoubleClick)  ||
       (evt == Button::Hold) || (evt == Button::ShortHold) ) {
        return; // Not sending these events to Solo since they aren't used.
    }

    // store event for forwarding
    if (pendingEventIdxs.full()) {
        DBG(("button event dropped\n"));
        return;
    }

    ButtonEvent &be = events[eventIdx];
    be.id = b->id();
    be.event = evt;
    be.allButtonsMask = buttonPressMask;

    pendingEventIdxs.enqueue(eventIdx);
    eventIdx = (eventIdx + 1) % arraysize(events);
    HostProtocol::instance.requestTransaction();
}

void ButtonManager::onFlightModeChanged(FlightManager::FlightMode fm)
{
    // XXX: will need to be smarter once we can edit the
    //      function of the A and B buttons...
    button(Io::ButtonLoiter).setLed(fm == FlightManager::LOITER);
    button(Io::ButtonRTL).setLed(fm == FlightManager::RTL);
}

bool ButtonManager::producePacket(HostProtocol::Packet &p)
{
    if (pendingEventIdxs.empty()) {
        return false;
    }

    ButtonEvent &be = events[pendingEventIdxs.dequeue()];

    return HostMessage::appendButtonEvent(p, be.id, be.event, be.allButtonsMask);
}

void ButtonManager::shutdown()
{
    /*
     * we're shutting down - disable all visual elements.
     */

    for (unsigned i = 0; i < arraysize(buttons); ++i) {
        buttons[i].greenLedOff();
    }

    // Keep only the power button's wake interrupt active.
    for (unsigned i = 0; i < arraysize(buttons); ++i) {
        if (i != Io::ButtonPower) buttons[i].disableIRQ();
    }
    setBacklight(0);
}
