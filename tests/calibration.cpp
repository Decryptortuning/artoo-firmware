// Exercise actual Inputs loading/transaction and StickAxis math. Only the ADC
// driver and UI notification sinks are replaced; no hardware or control sends.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "src/stm32/hw.h"
#define ADC_H
class Adc {
public:
    static const uint16_t RawRange = 4096;
    enum SampleRate { SampleRate_239_5 };
    Adc(volatile ADC_t *, void (*)(bool)) {} // Do not retain/invoke ISR callback.
    void init(bool) { assert(false); }
    void setRegularSequence(unsigned, const uint8_t *) { assert(false); }
    void setSampleRate(uint8_t, SampleRate) { assert(false); }
    void beginSequence(unsigned, uint16_t *) { assert(false); }
};
#include "src/ui_events.h"
#define UI_H
class Ui {
public:
    static Ui instance;
    struct Alerts { void dismissInvalidStickAlerts() {} } alertManager;
    void pendEvent(Event::ID) {}
};
// Include dependencies before granting the fixture access to Inputs' boot mask.
#include "src/stickaxis.h"
#include "src/hostprotocol.h"
#include "src/battery.h"
#include "src/stm32/hwtimer.h"
#define private public
#include "src/inputs.h"
#undef private
#include "src/inputs.cpp"

Ui Ui::instance;
Params Params::sys;
volatile ADC_t ADC1;
static uint32_t primask;
uint32_t artooTestDisableInterrupts() { uint32_t old = primask; primask = 1; return old; }
void artooTestRestoreInterrupts(uint32_t old) { primask = old; }
SysTime::Ticks SysTime::now() { return 0; }

static const Params::StickCalibration observed[6] = {
    {733,2299,3701}, {806,2261,3699}, {778,2304,3719},
    {754,2127,3511}, {896,2024,3257}, {1191,0,2751}
};

static void assertRejectedWithoutMutation(const Params::StickCalibration *candidate,
                                           const Params::StickConfig *config)
{
    const Params::StoredValues stored = Params::sys.storedValues;
    StickAxis live[6];
    memcpy(live, Inputs::sticks, sizeof live);
    const uint8_t mask = Inputs::invalidStickInputMask;
    assert(!Inputs::applyStickParams(candidate, config));
    assert(!memcmp(&stored, &Params::sys.storedValues, sizeof stored));
    assert(!memcmp(live, Inputs::sticks, sizeof live));
    assert(mask == Inputs::invalidStickInputMask);
}

int main()
{
    static_assert(sizeof(Params::StoredValues) == 172, "wire record must not change");
    auto &stored = Params::sys.storedValues;
    memset(&stored, 0xff, sizeof stored);
    memcpy(stored.sticks, observed, sizeof observed);
    for (unsigned raw = 0; raw < 4; ++raw) {
        stored.rcSticks[raw].input = 3 - raw;
        stored.rcSticks[raw].direction = raw == 0 ? 0 : 1;
    }
    Inputs::loadStickParams();
    Inputs::initStickValidState(); // same production boot validation, without ADC init
    assert(Inputs::invalidStickInputMask == 0 && Inputs::mappingValid);
    assert(!memcmp(stored.sticks, observed, sizeof observed));
    for (unsigned raw = 0; raw < 6; ++raw) {
        StickAxis &axis = Inputs::stick(static_cast<Io::StickID>(Inputs::mappedStickID(raw)));
        assert(axis.getRawID() == raw);
        axis.update(raw == 5 ? observed[raw].minVal : observed[raw].trim);
        assert(!axis.hasInvalidInput());
        if (raw < 5) assert(axis.angularValue() == 0.0f);
    }
    assert(Inputs::applyStickParams(observed, stored.rcSticks));
    Params::StickCalibration candidate[6];
    memcpy(candidate, observed, sizeof candidate);
    // Unused linear trim is preserved exactly, including erased-looking trim.
    for (uint16_t trim : {uint16_t(0), uint16_t(1191), uint16_t(2000), uint16_t(2751), uint16_t(65535)}) {
        candidate[5].trim = trim;
        assert(Inputs::applyStickParams(candidate, stored.rcSticks));
        assert(!memcmp(stored.sticks, candidate, sizeof candidate));
        StickAxis &dial = Inputs::stick(Io::StickGimbalRate);
        for (auto direction : {StickAxis::Forward, StickAxis::Reverse}) {
            dial.setDirection(direction);
            for (unsigned sample = 1191; sample <= 2751; ++sample) {
                dial.update(sample);
                const float expected = float(sample - 1191) / 1560;
                const float got = dial.linearValue();
                const float delta = got - (direction == StickAxis::Forward ? expected : 1-expected);
                assert(delta > -0.00001f && delta < 0.00001f);
                assert(dial.angularValue() == 0.0f && !dial.hasInvalidInput());
            }
        }
    }
    // Strict angular boundaries/dead zones remain enforced, atomically.
    for (unsigned raw = 0; raw < 5; ++raw) {
        const unsigned dz = raw == 4 ? 250 : 1;
        memcpy(candidate, observed, sizeof candidate);
        for (uint16_t trim : {uint16_t(0), uint16_t(candidate[raw].minVal + dz),
                              uint16_t(candidate[raw].maxVal - dz), uint16_t(65535)}) {
            candidate[raw].trim = trim;
            assertRejectedWithoutMutation(candidate, stored.rcSticks);
        }
    }
    for (auto bad : {Params::StickCalibration{1191,0,1191}, {2751,0,1191},
                     {1191,0,4096}, {65535,65535,65535}}) {
        memcpy(candidate, observed, sizeof candidate); candidate[5] = bad;
        assertRejectedWithoutMutation(candidate, stored.rcSticks);
        // Invalid persisted endpoints remain invalid on boot and config-only apply.
        stored.sticks[5] = bad;
        Inputs::loadStickParams(); Inputs::initStickValidState();
        assert(Inputs::invalidStickInputMask == (1u << 5));
        assert(Inputs::applyStickParams(nullptr, stored.rcSticks));
        assert(Inputs::invalidStickInputMask == (1u << 5));
        assert(!memcmp(&stored.sticks[5], &bad, sizeof bad));
        assert(Inputs::stick(Io::StickGimbalRate).linearValue() == 0.5f);
        assert(Inputs::applyStickParams(observed, stored.rcSticks));
    }
    // Smallest and widest legal linear spans; no center/denominator dependency.
    for (auto endpoints : {Params::StickCalibration{0,65535,1}, {0,0,4095}}) {
        memcpy(candidate, observed, sizeof candidate); candidate[5] = endpoints;
        assert(Inputs::applyStickParams(candidate, stored.rcSticks));
        auto &dial = Inputs::stick(Io::StickGimbalRate); dial.setDirection(StickAxis::Forward);
        dial.update(endpoints.minVal); assert(dial.linearValue() == 0);
        dial.update(endpoints.maxVal); assert(dial.linearValue() == 1);
    }
    assert(primask == 0);
    puts("Actual calibration snapshot, boot mask, atomic apply, angular boundaries and linear math passed.");
}
