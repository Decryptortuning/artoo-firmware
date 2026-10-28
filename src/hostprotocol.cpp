#include "cameracontrol.h"
#include "hostprotocol.h"
#include "inputs.h"
#include "params.h"
#include "tasks.h"
#include "flightmanager.h"
#include "vehicleconnector.h"
#include "powermanager.h"
#include "buttonmanager.h"
#include "buttonfunction.h"
#include "lockout.h"
#include "selftest.h"
#include "factorytest.h"
#include "version.h"
#include "mavlink.h"
#include "board.h"
#include "ui.h"
#include "ui_events.h" // Debugging only

#include "stm32/gpio.h"
#include "stm32/sys.h"

HostProtocol HostProtocol::instance(Usart(&HOST_UART, onTxComplete));

void HostProtocol::init()
{
    rxbuf.init();
    rxDecoder.reset();

    /*
     * we want to enable uart RX immediately, in case
     * the imx6 has already booted somehow, so we can
     * skip to our running state.
     */
    uart.init(HOST_UART_RX_GPIO, HOST_UART_TX_GPIO, 115200, true);

    /*
     * but, we want to leave TX disabled so we don't
     * inadvertently backpower the imx6 before we've
     * actually enabled the voltage rails.
     */
    disableTX();
}

void HostProtocol::enableTX()
{
    InterruptGuard guard;
    txSchedule.enable();
    // reattach uart tx line to the uart peripheral
    GPIOPin tx = HOST_UART_TX_GPIO;
    tx.setControl(GPIOPin::OUT_ALT_50MHZ);

    NVIC.irqEnable(IVT.HOST_UART_DMA_TX_CH);
    Tasks::trigger(Tasks::HostProtocol);
}

void HostProtocol::disableTX()
{
    /*
     * We want to continue listening for msgs from the host,
     * but if it's disabled, it's possible for the TX line
     * to back power it.
     *
     * Set TX line to hi-z, but leave RX line enabled.
     */

    InterruptGuard guard;
    uart.cancelTx();
    txSchedule.disable();
    NVIC.irqDisable(IVT.HOST_UART_DMA_TX_CH);

    GPIOPin tx = HOST_UART_TX_GPIO;
    tx.setControl(GPIOPin::OUT_OPEN_2MHZ);
    tx.setHigh();

    firstMsgRxed = false;

    rxResetPending = true;
}

void HostProtocol::requestTransaction()
{
    // Producers may request from ISR context, but packet construction and DMA
    // buffer ownership belong exclusively to the foreground protocol task.
    Tasks::trigger(Tasks::HostProtocol);
}

void HostProtocol::producePacket()
{
    /*
     * Produce a packet to send to the host.
     *
     * Called only from task(); DMA completion merely requests another turn.
     */

    txPacket.reset();

    if (dispatchProduce() && txPacket.ok() && !txPacket.isEmpty()) {
        txSchedule.start();
        uart.writeDma(txPacket.data(), txPacket.length());
        stats.txBytes += txPacket.length();
    } else {
        if (!txPacket.ok()) ++stats.txRejected;
        txSchedule.complete();
    }
}

bool HostProtocol::dispatchProduce()
{
    /*
     * Helper for producePacket(), just concerned with
     * collecting a packet from one of our data sources.
     */

    if (produceHostProtocolPacket(txPacket)) {
        return true;
    }

    if (ButtonManager::producePacket(txPacket)) {
        return true;
    }

    if (Inputs::producePacket(txPacket)) {
        return true;
    }

    if (FlightManager::instance.producePacket(txPacket)) {
        return true;
    }

    if (VehicleConnector::instance.producePacket(txPacket)) {
        return true;
    }

    if (PowerManager::producePacket(txPacket)) {
        return true;
    }

    if (SelfTest::producePacket(txPacket)) {
        return true;
    }

    return false;
}

bool HostProtocol::produceHostProtocolPacket(Packet &p)
{
    /*
     * Produce a response to a request that we've
     * received from our host.
     */

    switch (hostRequest) {

    default:
        return false;

    case ParamStoredVals:
        p.delimitSlip();
        p.appendSlip(ParamStoredVals);
        p.appendItemSlip(Params::sys.storedValues);
        p.delimitSlip();
        break;

    case SysInfo:
        const uint16_t hwversion = BOARD;

        p.delimitSlip();
        p.appendSlip(SysInfo);
        p.appendSlip(Sys::UniqueId, Sys::UniqueIdLen);
        p.appendItemSlip(hwversion);
        p.appendSlip(Version::str(), strlen(Version::str()));
        p.delimitSlip();
        break;
    }

    hostRequest = Nop;
    return true;
}

void HostProtocol::onTxComplete(bool success)
{
    if (!success) ++instance.stats.txErrors;
    // A failed packet is dropped, never retried (it may contain a flight command).
    instance.txSchedule.complete();
    Tasks::trigger(Tasks::HostProtocol);
}

IRQ_HANDLER ISR_USART1()
{
    HostProtocol::instance.isr();
}

void HostProtocol::isr()
{
    uint8_t b;
    uint16_t status = uart.isr(b);

    if (status & Usart::SR_OVERRUN) {
        rxResetPending = true;
        stats.overruns++;

    } else if (status & (Usart::SR_PARITY_ERR | Usart::SR_NOISE_ERR | Usart::SR_FRAMING_ERR)) {
        // NE and FE show up at the same time as RXNE,
        // so check those first, and only accept byte if they're clear
        rxResetPending = true;
        stats.lineErrors++;

    } else if (status & Usart::SR_RXED) {
        if (!rxbuf.full()) {
            rxbuf.enqueue(b);
            Tasks::trigger(Tasks::HostProtocol);
        } else {
            stats.dropped++;
            rxResetPending = true;
        }
    }
    if (rxResetPending) Tasks::trigger(Tasks::HostProtocol);
}

void HostProtocol::task()
{
    unsigned budget = 256; // bound one invocation even under continuous RX
    while (budget-- && (!rxbuf.empty() || rxResetPending)) {
        if (rxResetPending) {
            // All queued bytes may straddle the lost byte. Drop that queue as
            // one unit, then require a fresh delimiter before accepting data.
            InterruptGuard guard;
            rxbuf.init();
            rxDecoder.reset();
            rxResetPending = false;
            ++stats.rejectedFrames;
            continue;
        }
        const uint8_t byte = rxbuf.dequeue();
        ++stats.rxBytes;
        const SlipDecoder<RawCapacity>::Result result = rxDecoder.consume(byte);
        if (result == SlipDecoder<RawCapacity>::Complete) {
            if (!rxResetPending) processPacket(rxDecoder.packet());
        } else if (result == SlipDecoder<RawCapacity>::Rejected) {
            ++stats.rejectedFrames;
        }
    }
    if (!rxbuf.empty() || rxResetPending) Tasks::trigger(Tasks::HostProtocol);
    if (txSchedule.ready() && uart.enabled()) producePacket();
}

void HostProtocol::processPacket(const RxPacket &p)
{
    static_assert(sizeof(Params::StoredValues::sticks) == CalibrationBytes, "calibration wire layout");
    static_assert(sizeof(Params::StoredValues::rcSticks) == StickConfigBytes, "axis wire layout");
    static_assert(sizeof(Params::SweepConfig) == SweepConfigBytes, "sweep wire layout");
    static_assert(sizeof(Params::StoredValues) == 172, "persistent/host parameter layout");
    if (p.isEmpty() || !p.ok() || !payloadValid(p.data()[0], p.data() + 1, p.payloadLen())) {
        ++stats.rejectedFrames;
        return;
    }
    firstMsgRxed = true;
    const uint8_t *bytes = p.data();

    switch (bytes[0]) {

    case SysInfo:
    case ParamStoredVals:
        hostRequest = static_cast<MsgID>(bytes[0]);
        requestTransaction();
        break;

    case SetRawIo:
        // first byte treated as bool
        Inputs::setRawIoEnabled(bytes[1] != 0);
        break;

    case Calibrate: {
        if (FlightManager::instance.armed()) break;
        Params::StickCalibration calibration[6];
        memcpy(calibration, bytes + 1, sizeof calibration);
        if (!Inputs::applyStickParams(calibration, Params::sys.storedValues.rcSticks))
            ++stats.rejectedFrames;
        break;
    }

    case ConfigSweepTime: {
        Params::SweepConfig config;
        memcpy(&config, bytes + 1, sizeof config);
        if (!config.isValid()) { ++stats.rejectedFrames; break; }
        InterruptGuard guard;
        Params::sys.storedValues.sweepConfig = config;
        CameraControl::instance.setSweepConfig(config);
        Params::sys.mark();
        break;
    }

    case ConfigStickAxes: {
        if (FlightManager::instance.armed()) break;
        Params::StickConfig configs[6];
        memcpy(configs, bytes + 1, sizeof configs);
        if (!Inputs::applyStickParams(nullptr, configs)) ++stats.rejectedFrames;
        break;
    }

    case ButtonFunctionCfg: {
        static_assert(sizeof(ButtonFunction::Config) == ButtonConfigBytes, "button wire layout");
        ButtonFunction::Config cfg = {};
        if (decodeButtonConfig(&cfg, &bytes[1], p.payloadLen())) {
            Io::ButtonID id = static_cast<Io::ButtonID>(cfg.buttonID);
            if (ButtonManager::setButtonFunction(id, &cfg)) {
                Params::sys.mark();
                Ui::instance.pendEvent(Event::ButtonFunctionUpdated);
            }
        }
        break;
    }

    case Mavlink:
        Mavlink::onMavlinkData(&bytes[1], p.payloadLen());
        break;

    case PairRequest:
        VehicleConnector::instance.onPairingRequest(&bytes[1], p.payloadLen());
        break;

    case PairResult:
        VehicleConnector::instance.onPairingResult(&bytes[1], p.payloadLen());
        break;

    case OutputTest:
        FactoryTest::onOutputTest(&bytes[1], p.payloadLen());
        break;

    case SetShotInfo: {
        char si[ShotTextBytes + 1];
        copyText(si, sizeof si, &bytes[1], p.payloadLen());
        Ui::instance.gimbal.onShotChanged(si);
        Ui::instance.topBar.onShotChanged(si);
    } break;

    case Updater:
        onUpdaterMsg(&bytes[1], p.payloadLen());
        break;

    case LockoutState:
        if (p.payloadLen() >= 1) {
            Lockout::onHostProtoMsg(bytes[1]);
        }
        break;

    case SelfTest:
        static const uint64_t SelfTestMagic = 0x73656c6674657374;
        if (p.payloadLen() >= sizeof(SelfTestMagic) &&
            memcmp(&bytes[1], &SelfTestMagic, sizeof SelfTestMagic) == 0)
        {
            SelfTest::checkForShorts();
        }
        break;

    case GpioTest:
        FactoryTest::onGpioTest(&bytes[1], p.payloadLen());
        break;

    case TestEvent:
        if (p.payloadLen() >= 1){
            if (Event::isValid(bytes[1])) {
                Event::ID event = static_cast<Event::ID>(bytes[1]);
                Ui::instance.pendEvent(event);
            } else {
                DBG(("[host_protocol.cpp] Received event is invalid.\n"));
            }
        }
        break;

    case SetTelemUnits:
        if (p.payloadLen() >= 1){
            Ui::instance.telem.onUnitsChanged(bytes[1]);
        }
        break;

    case SoloAppConnection: // TODO: If there's more of these iMX6->STM32 type alerts we chould think about combining them all together, leaving like this for now
        if (p.payloadLen() >= 1){
            Ui::instance.telem.onSoloAppConnChanged(bytes[1]);
        }
        break;
    }
}

void HostProtocol::onUpdaterMsg(const uint8_t *bytes, unsigned len)
{
    // XXX: find a better home for this some day

    if (len >= 1) {
        switch (bytes[0]) {
        case 0: // begin
        case 1: // success
        case 2: // fail
            Ui::instance.pendEvent(static_cast<Event::ID>(Event::SystemUpdateBegin + bytes[0]));
            break;
        }
    }
}
