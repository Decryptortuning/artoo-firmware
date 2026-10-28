#ifndef COMMANDSTATE_H
#define COMMANDSTATE_H

#include <stdint.h>

// Tracks intent/response identity only. This class never sends or retries a
// vehicle command. FlightManager owns serialization and response side effects.
struct CommandState {
    enum State { Complete, Pending, Sent };
    enum ID { None, SetFlightMode, SetArmState, GetHomeWaypoint, Takeoff,
              FlyButtonClick, FlyButtonHold, PauseButtonClick };
    enum Response { Ack, MissionItem, ModeState };
    ID id;
    State state;
    uint32_t argument;
    uint16_t wireID;
    Response response;
    int64_t deadline;

    CommandState() : id(None), state(Complete), argument(0), wireID(0),
                     response(Ack), deadline(0) {}

    static unsigned priority(ID operation, uint32_t value) {
        if (operation == SetArmState && value != 1) return 3; // disarm/force disarm
        if (operation == SetFlightMode && (value == 6 || value == 9 || value == 21)) return 2;
        if (operation == PauseButtonClick) return 2;
        return operation == GetHomeWaypoint ? 0 : 1;
    }

    bool schedule(ID operation, uint32_t value = 0) {
        // New intent may replace an unsent request, but background/home or
        // normal UI activity must not displace an unsent emergency request.
        if (operation == GetHomeWaypoint && state != Complete) return false;
        if (state == Pending && priority(operation, value) < priority(id, argument)) return false;
        id = operation;
        argument = value;
        state = Pending;
        deadline = 0;
        return true;
    }

    void sent(uint16_t wire, Response expected, int64_t expires) {
        wireID = wire;
        response = expected;
        deadline = expires;
        state = Sent;
    }

    // MAVLink 1 has no transaction nonce. A late response for a replaced
    // operation with the SAME wire command (e.g. arm/disarm 400) is ambiguous;
    // this identity check cannot establish which transmission was acknowledged.
    bool acknowledge(uint16_t wire, bool fromVehicle) {
        if (!fromVehicle || state != Sent || response != Ack || wire != wireID) return false;
        state = Complete;
        return true;
    }

    bool missionItem(unsigned sequence, bool fromVehicle) {
        if (!fromVehicle || state != Sent || response != MissionItem || sequence != argument) return false;
        state = Complete;
        return true;
    }

    bool confirmMode(unsigned mode, bool fromVehicle) {
        if (!fromVehicle || state != Sent || response != ModeState || mode != argument) return false;
        state = Complete;
        return true;
    }

    bool expire(int64_t now) {
        if (state != Sent || now < deadline) return false;
        state = Complete;
        return true;
    }
};

#endif
