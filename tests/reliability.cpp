#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include "src/paramvalidation.h"
#include "src/paramstore.h"
#include "src/commandstate.h"
#include "src/stm32/irqguard.h"
#include "src/button.h"
#include "src/stickaxis.h"
#include "src/inputs.h"

volatile EXTI_t EXTI;
static uint32_t primask;
uint32_t artooTestDisableInterrupts() { const uint32_t old = primask; primask = 1; return old; }
void artooTestRestoreInterrupts(uint32_t mask) { primask = mask; }

struct FlashModel {
    static bool locked, programming, eraseFail, finishFail, mismatch;
    static unsigned writes;
    static int failAt;
    static uint16_t memory[86];
    static void reset() {
        locked = true; programming = eraseFail = finishFail = mismatch = false;
        writes = 0; failAt = -1; memset(memory, 0, sizeof memory);
    }
    static void unlock() { locked = false; }
    static void lock() { locked = true; }
    static void clearStatus() { assert(!locked); }
    static bool erasePage(uint32_t address) {
        assert(address == 0x0803f800 && !locked);
        if (eraseFail) return false;
        memset(memory, 0xff, sizeof memory);
        return true;
    }
    static void beginProgramming() { assert(!locked); programming = true; }
    static bool programHalfWord(uint16_t value, uint32_t address) {
        assert(!locked && programming && address == 0x0803f800 + writes * 2);
        if (static_cast<int>(writes) == failAt) return false;
        memory[writes++] = value;
        return true;
    }
    static bool endProgramming() { programming = false; return !finishFail; }
    static uint16_t readHalfWord(uint32_t address) {
        assert(locked && !programming);
        return memory[(address - 0x0803f800) / 2] ^ (mismatch ? 1 : 0);
    }
};
bool FlashModel::locked, FlashModel::programming, FlashModel::eraseFail,
     FlashModel::finishFail, FlashModel::mismatch;
unsigned FlashModel::writes;
int FlashModel::failAt;
uint16_t FlashModel::memory[86];

static void flashFaults()
{
    uint8_t record[172];
    for (unsigned i = 0; i < sizeof record; ++i) record[i] = i;
    FlashModel::reset();
    assert(ParamStore::write<FlashModel>(0x0803f800, record, sizeof record));
    assert(FlashModel::locked && !FlashModel::programming);
    assert(!memcmp(record, FlashModel::memory, sizeof record));
    for (int position = 0; position < 86; ++position) {
        FlashModel::reset(); FlashModel::failAt = position;
        assert(!ParamStore::write<FlashModel>(0x0803f800, record, sizeof record));
        assert(FlashModel::locked && !FlashModel::programming);
        assert(FlashModel::writes == static_cast<unsigned>(position));
    }
    FlashModel::reset(); FlashModel::eraseFail = true;
    assert(!ParamStore::write<FlashModel>(0x0803f800, record, sizeof record));
    assert(FlashModel::locked && !FlashModel::programming && FlashModel::writes == 0);
    FlashModel::reset(); FlashModel::finishFail = true;
    assert(!ParamStore::write<FlashModel>(0x0803f800, record, sizeof record));
    assert(FlashModel::locked && !FlashModel::programming && FlashModel::writes == 86);
    FlashModel::reset(); FlashModel::mismatch = true;
    assert(!ParamStore::write<FlashModel>(0x0803f800, record, sizeof record));
    assert(FlashModel::locked && !FlashModel::programming);
    // A new independent transaction can recover; no mass erase/option bytes.
    FlashModel::reset();
    assert(ParamStore::write<FlashModel>(0x0803f800, record, sizeof record));
}

// The only dependency of actual StickAxis::update that is replaced: record
// diagnostic notification without UI, serial or vehicle side effects.
static unsigned invalidSamples;
void Inputs::onInvalidStickInput(Io::AdcID, uint16_t, uint16_t, uint16_t, uint16_t) {
    ++invalidSamples;
}

static void realStickMath()
{
    Params::StickConfig cfg;
    memset(&cfg, 0xff, sizeof cfg);
    for (unsigned dz : {0u, 1u, 250u}) {
        StickAxis axis(Io::AdcStick0, StickAxis::Forward, dz);
        Params::StickCalibration invalid = {2000,2000,2000};
        axis.init(invalid, cfg, Io::StickRoll);
        axis.update(2000);
        assert(axis.angularValue() == 0.0f && axis.linearValue() == 0.5f);
        Params::StickCalibration valid = {100,2000,3900};
        axis.init(valid, cfg, Io::StickRoll);
        for (unsigned sample = 100; sample <= 3900; ++sample) {
            axis.update(sample);
            const float angle = axis.angularValue(), linear = axis.linearValue();
            assert(angle >= -1.0f && angle <= 1.0f && linear >= 0.0f && linear <= 1.0f);
        }
        axis.setCalibration(invalid);
        assert(axis.angularValue() == 0.0f && axis.linearValue() == 0.5f);
    }
    assert(invalidSamples == 3);
}

static void pendingSaves()
{
    ParamStore::PendingWrite pending = {};
    pending.mark();
    const uint32_t first = pending.generation;
    pending.complete(first, false);
    assert(pending.dirty); // failed erase/program/readback remains pending
    pending.mark(); // ISR edits after the flash snapshot
    pending.complete(first, true);
    assert(pending.dirty);
    pending.complete(pending.generation, true);
    assert(!pending.dirty);
}

static void parameterDomains()
{
    for (unsigned dz : {0u, 1u, 250u}) {
        assert(ParamValidation::calibration(100, 2000, 3900, dz));
        assert(!ParamValidation::calibration(2000, 2000, 3900, dz));
        assert(!ParamValidation::calibration(100, 3900, 3900, dz));
        assert(!ParamValidation::calibration(3000, 2000, 3900, dz));
        assert(!ParamValidation::calibration(0, 2000, 4096, dz));
        assert(!ParamValidation::calibration(0xffff, 0xffff, 0xffff, dz));
        assert(!ParamValidation::calibration(100, 100 + dz, 3900, dz));
    }
    struct Config { uint8_t input, direction, expo, reserved; uint32_t extra; } cfg[6];
    static_assert(sizeof cfg == 48, "fixture wire fields");
    memset(cfg, 0xff, sizeof cfg);
    assert(ParamValidation::mappings(cfg)); // Every independent sentinel remains valid.
    const uint8_t modes[][4] = {{3,0,1,2},{3,2,1,0}};
    for (const auto &mode : modes) {
        for (unsigned i = 0; i < 4; ++i) cfg[i].input = mode[i];
        assert(ParamValidation::mappings(cfg));
    }
    for (unsigned direction = 0; direction < 256; ++direction) {
        cfg[0].direction = direction;
        assert(ParamValidation::mappings(cfg)); // nonzero direction was never restricted to 1
    }
    cfg[1].input = cfg[0].input;
    assert(!ParamValidation::mappings(cfg));
    memset(cfg, 0xff, sizeof cfg); cfg[0].input = 4;
    assert(!ParamValidation::mappings(cfg));
    memset(cfg, 0xff, sizeof cfg); cfg[4].input = 5;
    assert(!ParamValidation::mappings(cfg));
    memset(cfg, 0xff, sizeof cfg); cfg[0].expo = 101;
    assert(!ParamValidation::mappings(cfg));
    assert(ParamValidation::sweep(3,90));
    assert(!ParamValidation::sweep(0,90) && !ParamValidation::sweep(90,3));
    assert(!ParamValidation::sweep(1,UINT32_MAX));
    assert(ParamValidation::cameraPreset(0) && ParamValidation::cameraPreset(90));
    assert(!ParamValidation::cameraPreset(-1) && !ParamValidation::cameraPreset(91));
    for (uint32_t bits : {0x7f800000u,0xff800000u,0x7fc00000u,0xffffffffu}) {
        float value; memcpy(&value,&bits,sizeof value);
        assert(!ParamValidation::cameraPreset(value));
    }
}

static void commandLifecycle()
{
    CommandState c;
    assert(c.schedule(CommandState::SetArmState, 1));
    c.sent(400, CommandState::Ack, 1000);
    assert(!c.acknowledge(2,true)); // old local-enum vs wire-ID defect
    assert(!c.acknowledge(400,false)); // wrong source
    assert(c.acknowledge(400,true));
    assert(!c.acknowledge(400,true)); // duplicate response has no effect
    assert(c.schedule(CommandState::Takeoff));
    assert(c.schedule(CommandState::SetArmState,2)); // force disarm replaces pending takeoff
    assert(!c.schedule(CommandState::GetHomeWaypoint));
    assert(!c.schedule(CommandState::FlyButtonClick));
    assert(c.id == CommandState::SetArmState && c.argument == 2);
    c.sent(400,CommandState::Ack,2000);
    assert(!c.expire(1999));
    assert(c.expire(2000));
    assert(!c.acknowledge(400,true)); // late ACK cannot advance a timed-out operation
    assert(c.schedule(CommandState::SetFlightMode,9));
    assert(!c.schedule(CommandState::Takeoff)); // unsent land is preserved
    assert(c.schedule(CommandState::SetArmState,0)); // disarm still wins
    c.sent(400,CommandState::Ack,3000);
    assert(!c.schedule(CommandState::GetHomeWaypoint,0)); // background query preserves outstanding ACK
    assert(c.acknowledge(400,true));
    assert(c.schedule(CommandState::GetHomeWaypoint,0));
    c.sent(40,CommandState::MissionItem,4000);
    assert(!c.acknowledge(40,true));
    assert(!c.missionItem(1,true) && !c.missionItem(0,false));
    assert(c.missionItem(0,true));
    assert(c.schedule(CommandState::SetFlightMode,5));
    c.sent(11,CommandState::ModeState,5000);
    assert(!c.acknowledge(11,true));
    assert(!c.confirmMode(6,true) && !c.confirmMode(5,false));
    assert(c.confirmMode(5,true));
}

static void masks()
{
    primask = 0;
    { InterruptGuard outer; assert(primask == 1);
      { InterruptGuard inner; assert(primask == 1); }
      assert(primask == 1); }
    assert(primask == 0);
    primask = 1;
    { InterruptGuard nested; }
    assert(primask == 1);
    alignas(16) GPIO_t port = {};
    for (unsigned pin = 0; pin < 16; ++pin) {
        GPIOPin gpio(&port,pin);
        Button b(gpio,gpio,gpio,Io::ButtonA);
        EXTI.IMR = 0xffff;
        b.disableIRQ(); // actual production Button helper and GPIO register method
        assert(EXTI.IMR == (0xffffu & ~(1u << pin)));
        b.enableIRQ();
        assert(EXTI.IMR == 0xffff);
    }
}

int main()
{
    flashFaults(); pendingSaves(); parameterDomains(); realStickMath(); commandLifecycle(); masks();
    puts("Flash fault injection, parameter domains, command identity/priority, and actual button/IRQ masks passed.");
}
