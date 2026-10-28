#include "inputs.h"
#include "buttonmanager.h"
#include "dsm.h"
#include "params.h"
#include "board.h"
#include "ui.h"

#include "stm32/gpio.h"
#include "stm32/adc.h"

Adc Inputs::Adc1(&ADC1, Inputs::adcConversionComplete);

uint16_t Inputs::adcSamples[];
uint16_t Inputs::completedSamples[];
Inputs::InvalidStickValues Inputs::invalidStickVals;
SysTime::Ticks Inputs::alertStateEnterTimestamp;

bool Inputs::mappingValid;
bool Inputs::captureFault;
bool Inputs::reportRequested;
bool Inputs::rawIoEnabled;
uint8_t Inputs::invalidStickInputMask;

// matches order of StickID
StickAxis Inputs::sticks[] = {
    StickAxis(Io::AdcStick0, StickAxis::Forward, StickAxis::RcChannelDeadZone),     // StickThro
    StickAxis(Io::AdcStick1, StickAxis::Forward, StickAxis::RcChannelDeadZone),     // StickRoll
    StickAxis(Io::AdcStick2, StickAxis::Forward, StickAxis::RcChannelDeadZone),     // StickPitch
    StickAxis(Io::AdcStick3, StickAxis::Reverse, StickAxis::RcChannelDeadZone),     // StickYaw
    StickAxis(Io::AdcGimbalY, StickAxis::Reverse, StickAxis::GimbalPitchDeadZone),  // StickGimbalY
    StickAxis(Io::AdcGimbalRate, StickAxis::Reverse, 0)                             // StickGimbalRate
};

void Inputs::init()
{
    Dsm::instance.init();
    initADC();
    initStickValidState();
    captureFault = true;
    beginAdcCapture();
}

void Inputs::initADC()
{
    GPIOPin adcPins[] = {
        STICK_0_GPIO,
        STICK_1_GPIO,
        STICK_2_GPIO,
        STICK_3_GPIO,
        GIMBAL_RATE_GPIO,
        GIMBAL_Y_GPIO,
#if (BOARD >= BOARD_BB03)
        AMBIENT_LIGHT_GPIO,
        CELL_THERM_GPIO,
        CELL_ID_GPIO,
        CELL_MEAS_BOT_GPIO,
        CELL_MEAS_TOP_GPIO,
#endif
    };

    for (unsigned i = 0; i < arraysize(adcPins); ++i) {
        adcPins[i].setControl(GPIOPin::IN_ANALOG);
    }

    const uint8_t adcChannels[] = {
        STICK_0_ADC_CH,
        STICK_1_ADC_CH,
        STICK_2_ADC_CH,
        STICK_3_ADC_CH,
        GIMBAL_Y_ADC_CH,
        GIMBAL_RATE_ADC_CH,
#if (BOARD >= BOARD_BB03)
        AMBIENT_LIGHT_ADC_CH,
        CELL_THERM_ADC_CH,
        CELL_ID_ADC_CH,
        CELL_MEAS_BOT_ADC_CH,
        CELL_MEAS_TOP_ADC_CH,
#endif
    };

    Adc1.init(true);
    Adc1.setRegularSequence(arraysize(adcChannels), adcChannels);
    for (unsigned i = 0; i < arraysize(adcChannels); ++i) {
        Adc1.setSampleRate(adcChannels[i], Adc::SampleRate_239_5);
    }

    loadStickParams();
}

void Inputs::loadStickParams()
{
    /*
     * Load the parameters for a given stick.
     * Note: params are stored with the raw ID for a stick input.
     */

    const Params::StoredValues &sv = Params::sys.storedValues;
    mappingValid = ParamValidation::mappings(sv.rcSticks);
    if (!mappingValid) {
        invalidStickInputMask = 0x3f;
        return;
    }
    for (unsigned rawID = 0; rawID < arraysize(sticks); ++rawID) {
        unsigned mappedID = mappedStickID(rawID);
        sticks[mappedID].setRawID(static_cast<Io::AdcID>(rawID));
        sticks[mappedID].init(sv.sticks[rawID], sv.rcSticks[rawID], mappedID);
    }
}

void Inputs::adcConversionComplete(bool success)
{
    /*
     * Called in ISR context.
     */

    captureFault = !success;
    if (!success) {
        // Never consume partially DMA-written samples as a valid control frame.
        // Restart acquisition; control validity remains false until completion.
        beginAdcCapture();
        return;
    }
    memcpy(completedSamples, adcSamples, sizeof completedSamples);
    static const unsigned NUM_RC_STICKS = 4;

    if (mappingValid) {
        for (unsigned rawID = 0; rawID < NUM_RC_STICKS; ++rawID) {
            unsigned mappedID = mappedStickID(rawID);
            sticks[mappedID].update(adcSamples[rawID]);
        }

        sticks[Io::StickGimbalY].update(adcSamples[Io::AdcGimbalY]);
        sticks[Io::StickGimbalRate].update(adcSamples[Io::AdcGimbalRate]);
    } else {
        onInvalidStickInput(Io::AdcStick0, adcSamples[0], 0xffff, 0xffff, 0xffff);
    }

#if (BOARD >= BOARD_BB03)
    if (Battery::instance.samplesEnabled()) {
        Battery::instance.onCellSamples(adcSamples[Io::AdcCellBottom], adcSamples[Io::AdcCellTop],
                                        adcSamples[Io::AdcCellTherm], adcSamples[Io::AdcCellID]);
    }
#endif

    Tasks::trigger(Tasks::Camera);

    beginAdcCapture();
}

bool Inputs::producePacket(HostProtocol::Packet &pkt)
{
    /*
     * Our opportunity to produce a packet to be sent to the host.
     */

    // always send an alert if out of range stick values are being received,
    // optionally followed by regularly scheduled input reports.

    // Worst-case encoded aggregate: invalid-stick (4+4*2 bytes), DSM
    // (8 channels), input report (3 uint16), raw ADC samples + button mask.
    // Each frame has two delimiters and one unescaped host ID.
    static_assert(HostProtocol::TxCapacity >=
                  (3 + 2 * (sizeof(Io::AdcID) + 4 * sizeof(uint16_t))) +
                  (3 + 2 * 8 * sizeof(uint16_t)) +
                  (3 + 2 * 3 * sizeof(uint16_t)) +
                  (3 + 2 * (sizeof(adcSamples) + sizeof(uint16_t))),
                  "encoded input aggregate exceeds TX storage");

    bool appendedInvalidStickVals = invalidStickVals.producePacket(pkt);

    if (!reportRequested) {
        return appendedInvalidStickVals;
    }

    reportRequested = false;

    StickAxis axes[6];
    uint16_t rawSamples[Io::AdcCount];
    uint8_t invalidMask;
    bool validMapping;
    {
        InterruptGuard guard;
        for (unsigned i = 0; i < 6; ++i) axes[i] = sticks[i];
        memcpy(rawSamples, completedSamples, sizeof rawSamples);
        invalidMask = invalidStickInputMask;
        validMapping = mappingValid && !captureFault;
    }

    // RC channels to be forwarded to the vehicle, don't send any values (to trigger RCFailsafe) if getting any invalid values from flight sticks
    if (validMapping && !(invalidMask & 0x0f)) {
        Dsm::instance.producePacket(pkt, axes, invalidMask);
    }

    // additional inputs that 3rd parties may be interested in
    uint16_t batt = Battery::instance.level();

    pkt.delimitSlip();
    pkt.appendSlip(HostProtocol::InputReport);
    pkt.appendItemSlip(axes[Io::StickGimbalY].scaledAngularValue());
    pkt.appendItemSlip(axes[Io::StickGimbalRate].scaledLinearValue());
    pkt.appendItemSlip(batt);
    pkt.delimitSlip();

    // usually only enabled for testing purposes
    if (rawIoEnabled) {
        pkt.delimitSlip();
        pkt.appendSlip(HostProtocol::RawIoReport);
        pkt.appendSlip(rawSamples, sizeof rawSamples);
        uint16_t buttonMask = ButtonManager::pressMask();
        pkt.appendSlip(&buttonMask, sizeof(buttonMask));
        pkt.delimitSlip();
    }

    return true;
}

bool Inputs::InvalidStickValues::producePacket(HostProtocol::Packet &pkt)
{
    /*
     * If we have invalid values, write them to host,
     * and reset values once we've done so.
     *
     * We rely on the fact that we will continue to receive
     * invalid stick readings as long as they remain invalid.
     */

    InvalidStickValues snapshot;
    {
        InterruptGuard guard;
        if (!isSet) return false;
        snapshot = *this;
        reset();
    }
    pkt.delimitSlip();
    pkt.appendSlip(HostProtocol::InvalidStickInputs);
    pkt.appendItemSlip(snapshot.rawID);
    pkt.appendItemSlip(snapshot.inputVal);
    pkt.appendItemSlip(snapshot.trimVal);
    pkt.appendItemSlip(snapshot.minVal);
    pkt.appendItemSlip(snapshot.maxVal);
    pkt.delimitSlip();

    return true;
}

void Inputs::onInvalidStickInput(Io::AdcID rawID, uint16_t inputVal, uint16_t trim, uint16_t minVal, uint16_t maxVal)
{
    /*
     * A stick has received invalid input.
     *
     * Mark the stick as invalid, and hang onto the values
     * so we can report them.
     */

    invalidStickInputMask |= BIT(rawID);

    // if we're past the cooldown period (or timestamp hasn't been set) and another alert isn't being sent, we can send alerts
    if (SysTime::now() - alertStateEnterTimestamp > SysTime::msTicks(1000)) {
        invalidStickVals.latch(rawID, inputVal, trim, minVal, maxVal);
        pendInvalidStickAlert(rawID);
        alertStateEnterTimestamp = SysTime::now();
    }
}

void Inputs::InvalidStickValues::latch(Io::AdcID adcID, uint16_t v, uint16_t trim, uint16_t min, uint16_t max)
{
    InterruptGuard guard;
    if (!isSet) {
        rawID = adcID;
        mappedID = static_cast<uint8_t>(mappedStickID(adcID));
        inputVal = v;
        trimVal = trim;
        minVal = min;
        maxVal = max;
        isSet = true;
    }
}

void Inputs::InvalidStickValues::reset()
{
    /*
     * Clear out any previous invalid values.
     *
     * This is called every time an invalid stick error notification packet
     * is sent out, since we rely on the fact that we will continue
     * to receive invalid stick readings as long as they remain invalid.
     */

    rawID = Io::AdcStick0;
    mappedID = 255;
    inputVal = 0;
    trimVal = 0;
    minVal = 0;
    maxVal = 0;
    isSet = false;
}

unsigned Inputs::mappedStickID(unsigned rawID)
{
    /*
     * Retrieve the mapped id for the given raw ID.
     *
     * Raw IDs are in terms of hardware: sticks 0-3.
     * Mapped IDs are in terms of control axes: throttle, roll, pitch, yaw.
     */

    if (rawID >= 6) return 0;
    const unsigned configured = Params::sys.storedValues.rcSticks[rawID].input;
    // This function can be used for diagnostics even while mapping is invalid.
    const unsigned mapped = ParamValidation::mapping(rawID, configured);
    return mapped < 6 ? mapped : ParamValidation::defaultMapping(rawID);
}

bool Inputs::isCameraControlValid() {
    InterruptGuard guard;
    const uint8_t mask = (BIT(Io::AdcGimbalY) | BIT(Io::AdcGimbalRate));
    return !captureFault && mappingValid && (invalidStickInputMask & mask) == 0;
}

bool Inputs::isFlightControlValid() {
    InterruptGuard guard;
    const uint8_t mask = (BIT(Io::AdcStick0) | BIT(Io::AdcStick1) | BIT(Io::AdcStick2) | BIT(Io::AdcStick3));
    return !captureFault && mappingValid && (invalidStickInputMask & mask) == 0;
}

bool Inputs::isCameraControl(Io::StickID id) {
    return (id == Io::StickGimbalY || id == Io::StickGimbalRate);
}

bool Inputs::isFlightControl(Io::StickID id) {
    return (id == Io::StickThro || id == Io::StickRoll|| id == Io::StickPitch || id == Io::StickYaw);
}

void Inputs::pendInvalidStickAlert(Io::AdcID id)
{
    Io::StickID mapped_id = static_cast<Io::StickID>(id);

    if (isFlightControl(mapped_id)) {
        if (!isFlightControlValid()) {
            Ui::instance.pendEvent(Event::ControllerValueOutOfRange);
        }
        return;
    }

    if (isCameraControl(mapped_id)) {
        if (!isCameraControlValid()) {
            Ui::instance.pendEvent(Event::CamControlValueOutOfRange);
        }
        return;
    }
}

void Inputs::initStickValidState()
{
    invalidStickVals.reset();
    invalidStickInputMask = 0;
    const Params::StoredValues &sv = Params::sys.storedValues;
    if (!mappingValid) {
        invalidStickInputMask = 0x3f;
        return;
    }
    for (unsigned raw = 0; raw < 6; ++raw) {
        const Params::StickCalibration &cal = sv.sticks[raw];
        const unsigned dz = raw == Io::AdcGimbalY ? StickAxis::GimbalPitchDeadZone :
                            raw == Io::AdcGimbalRate ? 0 : StickAxis::RcChannelDeadZone;
        if (!cal.isValid(static_cast<Io::AdcID>(raw), dz))
            invalidStickInputMask |= 1u << raw;
    }
}

void Inputs::resetInvalidStickError()
{
    { InterruptGuard guard; initStickValidState(); }
    Ui::instance.alertManager.dismissInvalidStickAlerts();
}

bool Inputs::applyStickParams(const Params::StickCalibration *calibration,
                              const Params::StickConfig *configs)
{
    // New transactions validate completely before touching live or stored state.
    // Boot loading deliberately uses loadStickParams instead: invalid persisted
    // maps must remain visible and lock out control until explicitly repaired.
    if (!ParamValidation::mappings(configs)) return false;
    if (calibration) {
        for (unsigned raw = 0; raw < 6; ++raw) {
            const unsigned dz = raw == Io::AdcGimbalY ? StickAxis::GimbalPitchDeadZone :
                                raw == Io::AdcGimbalRate ? 0 : StickAxis::RcChannelDeadZone;
            if (!calibration[raw].isValid(static_cast<Io::AdcID>(raw), dz)) return false;
        }
    }
    // Copy constructors do no floating-point work. Only the short snapshots and
    // publication are masked; the six exponent calculations and UI work are not.
    StickAxis prepared[6];
    {
        InterruptGuard guard;
        for (unsigned i = 0; i < 6; ++i) prepared[i] = sticks[i];
    }
    const Params::StickCalibration *cal = calibration ? calibration : Params::sys.storedValues.sticks;
    for (unsigned raw = 0; raw < 6; ++raw) {
        const unsigned mapped = ParamValidation::mapping(raw, configs[raw].input);
        prepared[mapped].setRawID(static_cast<Io::AdcID>(raw));
        prepared[mapped].init(cal[raw], configs[raw], mapped);
    }
    {
        InterruptGuard guard;
        if (calibration) memcpy(Params::sys.storedValues.sticks, calibration,
                                sizeof Params::sys.storedValues.sticks);
        // configs may already refer to storedValues during calibration.
        if (configs != Params::sys.storedValues.rcSticks)
            memcpy(Params::sys.storedValues.rcSticks, configs, sizeof Params::sys.storedValues.rcSticks);
        for (unsigned i = 0; i < 6; ++i) sticks[i] = prepared[i];
        mappingValid = true;
        initStickValidState();
        Params::sys.mark();
    }
    Ui::instance.alertManager.dismissInvalidStickAlerts();
    return true;
}
