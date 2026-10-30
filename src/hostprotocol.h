#ifndef HOSTPROTOCOL_H
#define HOSTPROTOCOL_H

#include <string.h>

#include "packetbuffer.h"
#include "hostmessage.h"
#include "slipdecoder.h"
#include "ringbuffer.h"
#include "txschedule.h"
#include "stm32/usart.h"
#include "stm32/vectors.h"

#include "mavlink/c_library/ardupilotmega/mavlink.h"

class HostProtocol : public HostMessage
{
public:
    HostProtocol(Usart u) :
        uart(u),
        txSchedule(),
        txPacket(), rxDecoder(),
        rxbuf(),
        stats(),
        rxResetPending(false),
        firstMsgRxed(false),
        hostRequest(Nop), diagnosticsPending(false), diagnosticsSeen(false),
        diagnosticsToken(0), diagnosticsLast(0)
    {}


    // One host ID plus the largest current MAVLink-1 frame. TX reserves
    // worst-case escaping and both delimiters; never use a raw size for SLIP.
    static const unsigned RawCapacity = 1 + MAVLINK_MAX_PACKET_LEN;
    static const unsigned TxCapacity = 2 * RawCapacity + 2;
    typedef PacketBuffer<TxCapacity> Packet;
    typedef PacketBuffer<RawCapacity> RxPacket;

    static HostProtocol instance;

    void init();
    void enableTX();
    void disableTX();
    void requestTransaction();

    bool connected() const {
        return firstMsgRxed;
    }
    void onHostDisconnected() {
        firstMsgRxed = false;
    }

    void task();

private:
    void producePacket();
    bool dispatchProduce();
    bool produceHostProtocolPacket(Packet &p);
    void processPacket(const RxPacket &p);
    void processMavlinkData(const uint8_t *bytes, unsigned len);

    void onUpdaterMsg(const uint8_t *bytes, unsigned len);

    void isr();
    friend void ISR_USART1();

    static void onTxComplete(bool success);

    Usart uart;
    TxSchedule txSchedule;

    Packet txPacket;
    SlipDecoder<RawCapacity> rxDecoder;
    RingBuffer<2048> rxbuf; // again, huge but we're not strapped for RAM yet

    struct Stats {
        uint32_t txBytes;
        uint32_t rxBytes;
        uint32_t malformedBytes;
        uint32_t overruns;
        uint32_t dropped;
        uint32_t lineErrors;
        uint32_t rejectedFrames;
        uint32_t txRejected;
        uint32_t txErrors;
    } stats;

    volatile bool rxResetPending;

    bool firstMsgRxed;

    MsgID hostRequest;
    bool diagnosticsPending, diagnosticsSeen;
    uint16_t diagnosticsToken;
    uint64_t diagnosticsLast;
    bool produceDiagnostics(Packet &p);
};

#endif // HOSTPROTOCOL_H
