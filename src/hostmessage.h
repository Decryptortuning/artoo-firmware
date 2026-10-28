#ifndef HOSTMESSAGE_H
#define HOSTMESSAGE_H

#include <stdint.h>
#include <string.h>
#include "packetbuffer.h"

// UART host message IDs and byte contracts, shared by decoding and host tests.
struct HostMessage {
    enum MsgID {
        Nop,                // Empty packet
        DsmChannels,        // DSM channels to be sent to vehicle
        Calibrate,          // Calibration data - store in nonvolatile mem
        SysInfo,            // HW ID & version info
        Mavlink,            // Mavlink data
        SetRawIo,           // control whether RawIoReports should be sent
        RawIoReport,        // io measurements, for assembly testing
        PairRequest,        // vehicle pair button has been pushed
        PairConfirm,        // artoo user has confirmed pair request
        PairResult,         // pairing process completed
        ShutdownRequest,    // system has received a shutdown request from the user
        ParamStoredVals,    // request/report a Params::StoredValues structure (see params.h)
        OutputTest,         // read/write the LEDs, buzzer, vibe motor
        ButtonEvent,        // push button event
        InputReport,        // 16-bit inputs not included in DSM reports
        ConfigStickAxes,    // specify which sticks to use for which control axes
        ButtonFunctionCfg,  // specify the function of A/B/Loiter buttons
        SetShotInfo,        // set the current shot
        Updater,            // screens for update flow
        LockoutState,       // update the lockout state of the system
        SelfTest,           // request/response for factory self test
        ConfigSweepTime,    // specify bounds of camera sweep time
        GpioTest,           // used during factory test to enable/disable some gpio lines
        TestEvent,          // debug only for testing event behaviours
        SetTelemUnits,      // set telemetry units to metric/imperial
        InvalidStickInputs, // out of range stick values received
        SoloAppConnection,  // solo app connection status (connected/disconnected)
    };

    static const unsigned CalibrationBytes = 36;
    static const unsigned StickConfigBytes = 48;
    static const unsigned ButtonConfigBytes = 24;
    static const unsigned SweepConfigBytes = 8;
    static const unsigned ShotTextBytes = 64;
    static const unsigned PairTextBytes = 32;

    template<unsigned Capacity>
    static bool appendButtonEvent(PacketBuffer<Capacity> &packet, uint8_t id,
                                  uint8_t event, uint16_t mask) {
        const uint8_t payload[] = {id, event, static_cast<uint8_t>(mask),
                                  static_cast<uint8_t>(mask >> 8)};
        return packet.delimitSlip() && packet.appendSlip(ButtonEvent) &&
            packet.appendSlip(payload, sizeof payload) && packet.delimitSlip();
    }

    static bool payloadLengthValid(unsigned id, unsigned length) {
        switch (id) {
        case Nop: case ParamStoredVals: return length == 0;
        case SysInfo: return length <= 1;
        case SetRawIo: case Updater: case LockoutState: case TestEvent:
        case SetTelemUnits: case SoloAppConnection: return length == 1;
        case Calibrate: return length == CalibrationBytes;
        case ConfigStickAxes: return length == StickConfigBytes;
        case ConfigSweepTime: return length == SweepConfigBytes;
        case ButtonFunctionCfg: return length >= 4 && length <= ButtonConfigBytes;
        case SetShotInfo: return length <= ShotTextBytes;
        case PairRequest: case PairResult: return length > 0 && length <= PairTextBytes;
        case OutputTest: return length == 5;
        case SelfTest: return length == 8;
        case GpioTest: return length == 2;
        case Mavlink: return length > 0; // The frame decoder bounds the raw envelope.
        default: return false;          // Outgoing-only and unknown IDs.
        }
    }

    static bool payloadValid(unsigned id, const uint8_t *bytes, unsigned length) {
        if (!payloadLengthValid(id, length)) return false;
        // Direct updater queries contain only ID3. The running Linux service's
        // SysInfo::ping() sends one literal space through PacketHandler.
        return id != SysInfo || length == 0 || bytes[0] == ' ';
    }

    // Text labels may fill their wire field without a NUL. Truncate to local
    // capacity minus one, always terminate, and never read past the payload.
    static void copyText(char *dst, unsigned capacity, const uint8_t *src, unsigned length) {
        if (!capacity) return;
        unsigned n = 0;
        while (n + 1 < capacity && n < length && src[n]) {
            dst[n] = static_cast<char>(src[n]);
            ++n;
        }
        memset(dst + n, 0, capacity - n);
    }

    // Pairing is an identity, not a display label: never truncate or accept a
    // hidden suffix. Linux sends at most 31 characters and one final NUL.
    static bool pairingTextValid(const uint8_t *src, unsigned length) {
        return length > 0 && length <= PairTextBytes && src[length - 1] == 0 &&
            memchr(src, 0, length - 1) == NULL;
    }

    static bool decodeButtonConfig(void *dst, const uint8_t *src, unsigned length) {
        if (!payloadLengthValid(ButtonFunctionCfg, length)) return false;
        uint8_t *out = static_cast<uint8_t*>(dst);
        memcpy(out, src, 4);
        copyText(reinterpret_cast<char*>(out + 4), ButtonConfigBytes - 4,
                 src + 4, length - 4);
        return true;
    }
};

#endif
