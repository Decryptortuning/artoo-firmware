#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <thread>
#include <mutex>
#include <vector>
#include "src/stm32/timecapture.h"
#include "src/stm32/irqguard.h"
#include "src/ringbuffer.h"
#include "src/txschedule.h"
#include "src/buttonmanager.h"
#include "src/stm32/dma.h"

static std::mutex irqMutex;
static thread_local uint32_t primask;
uint32_t artooTestDisableInterrupts() {
    uint32_t previous = primask;
    if (!previous) irqMutex.lock();
    primask = 1;
    return previous;
}
void artooTestRestoreInterrupts(uint32_t previous) {
    primask = previous;
    if (!previous) irqMutex.unlock();
}
static SysTime::Ticks fakeNow;
SysTime::Ticks SysTime::now() { return fakeNow; }
volatile EXTI_t EXTI;
volatile DMA_t DMA1, DMA2;
volatile RCC_t RCC;
alignas(256) static GPIO_t port;
void GPIOPin::setControl(Control c) const { GPIOPin::setControl(port(), pin(), c); }
void GPIOPin::irqInit() const {} // only architecture routing is mocked
struct EventRecord { Button::Event event; bool pressed; };
static std::vector<EventRecord> capturedEvents;
void ButtonManager::dispatchEvt(Button *button, Button::Event event) {
    capturedEvents.push_back({event, button->isPressed()});
}
extern "C" void ISR_DMA2_Channel4_5();

static void clockSnapshots()
{
    struct Counter {
        uint32_t first, second; bool pend; unsigned reads;
        uint32_t value() { return reads++ ? second : first; }
        bool pending() const { return pend; }
    };
    const int64_t period = 20000000;
    int64_t last = 0;
    Counter c = {999, 999, false, 0};
    assert(TimeCapture::now(c, 0, last, 999, period) == 0);
    c = {500,499,false,0};
    int64_t mid = TimeCapture::now(c,0,last,999,period);
    assert(mid >= 9999999 && mid <= 10000000);
    c = {0,999,false,0}; // rollover between VAL reads before pending is observed
    assert(TimeCapture::now(c,0,last,999,period) == period);
    c = {900,899,true,0};
    assert(TimeCapture::now(c,0,last,999,period) >= period + 1999999);
    c = {900,899,false,0}; // IRQ has now accounted for that pending period
    int64_t afterIrq = TimeCapture::now(c,period,last,999,period);
    assert(afterIrq >= period + 1999999 && afterIrq <= period + 2000000);
    // Multiple hardware rollovers can collapse into ONE pending bit. Publish
    // monotonic values without claiming to recover the missing elapsed periods.
    c = {1,0,true,0};
    int64_t high = TimeCapture::now(c,period,last,999,period);
    for (unsigned i = 0; i < 100; ++i) {
        c = {999,998,true,0};
        assert(TimeCapture::now(c,period,last,999,period) == high);
    }
    c = {999,998,false,0};
    assert(TimeCapture::now(c,2*period,last,999,period) >= high);
    // Coarse value above 32 bits remains exact in the same production function.
    last = 0; c = {999,999,false,0};
    assert(TimeCapture::now(c,INT64_C(0x100000000),last,999,period) == INT64_C(0x100000000));
}

static void queuePublication()
{
    struct Record { uint32_t sequence, inverse; };
    RingBuffer<32, Record> queue;
    for (unsigned i = 0; i < 31; ++i) assert(queue.enqueue({i,~i}));
    assert(!queue.enqueue({99,0}));
    for (unsigned i = 0; i < 31; ++i) assert(queue.dequeue().sequence == i);
    assert(queue.empty());
    const unsigned count = 500000;
    std::thread producer([&] {
        for (unsigned i = 0; i < count; ++i) {
            while (!queue.enqueue({i,~i})) std::this_thread::yield();
        }
    });
    for (unsigned i = 0; i < count; ++i) {
        while (queue.empty()) std::this_thread::yield();
        const Record item = queue.dequeue();
        assert(item.sequence == i && item.inverse == ~i);
    }
    producer.join();
    assert(queue.empty());
    queue.init();
    assert(queue.empty() && queue.writeAvailable() == 31);
}

static void guardedProducers()
{
    RingBuffer<32, uint32_t> queue;
    const unsigned count = 50000;
    auto publish = [&](unsigned id) {
        for (unsigned sequence = 0; sequence < count; ++sequence) {
            for (;;) {
                InterruptGuard guard;
                // Same guarded production queue primitives used by ISR/UI
                // producers. The host mutex models exclusion, not MCU timing.
                if (queue.enqueue((id << 24) | sequence)) break;
            }
        }
    };
    std::thread first(publish,0), second(publish,1);
    unsigned expected[2] = {};
    for (unsigned received = 0; received < 2*count;) {
        if (queue.empty()) { std::this_thread::yield(); continue; }
        const uint32_t item = queue.dequeue();
        const unsigned id = item >> 24;
        assert(id < 2 && (item & 0xffffff) == expected[id]++);
        ++received;
    }
    first.join(); second.join();
    assert(expected[0] == count && expected[1] == count);
}

static void buttonGestures()
{
    const GPIOPin gpio(&port, 0);
    Button button(gpio,gpio,gpio,Io::ButtonFly);
    port.IDR = 1;
    button.init(); // boot-held input is captured without a synthetic Press
    assert(button.isPressed() && capturedEvents.empty());
    fakeNow = SysTime::msTicks(2800);
    button.pollForHold();
    assert(capturedEvents.size() == 3 && capturedEvents[2].event == Button::LongHold);
    button.pollForHold(); assert(capturedEvents.size() == 3);
    button.handleEdge(false,SysTime::msTicks(3000));
    assert(capturedEvents.back().event == Button::LongHoldRelease && !capturedEvents.back().pressed);
    capturedEvents.clear();
    fakeNow = SysTime::sTicks(99); // foreground was delayed; use captured durations
    button.handleEdge(true,SysTime::msTicks(4000));
    button.handleEdge(false,SysTime::msTicks(4100));
    assert(capturedEvents.size() == 3 && capturedEvents[0].event == Button::Press &&
           capturedEvents[2].event == Button::ClickRelease);
    capturedEvents.clear();
    port.IDR = 1;
    button.resynchronize(SysTime::msTicks(5000)); // lost-edge policy
    fakeNow = SysTime::sTicks(100);
    button.pollForHold(); assert(capturedEvents.empty());
    button.handleEdge(false,SysTime::msTicks(6000));
    assert(capturedEvents.empty()); // quarantined release emits no command gesture
    button.handleEdge(true,SysTime::msTicks(7000));
    button.handleEdge(false,SysTime::msTicks(7100));
    assert(capturedEvents.size() == 3 && capturedEvents.back().event == Button::ClickRelease);
}

static unsigned callback4, callback5;
static uint8_t flags4, flags5;
static void channel4(void *, uint8_t flags) { ++callback4; flags4 = flags; }
static void channel5(void *, uint8_t flags) { ++callback5; flags5 = flags; }
static void dmaRegisters()
{
    assert(!Dma::initChannel(&DMA1,-1,channel4,nullptr));
    assert(!Dma::initChannel(&DMA2,5,channel4,nullptr));
    assert(Dma::initChannel(&DMA2,3,channel4,nullptr));
    assert(Dma::initChannel(&DMA2,4,channel5,nullptr));
    DMA2.ISR = (Dma::Complete << 12) | (Dma::Error << 16);
    ISR_DMA2_Channel4_5(); // actual production shared vector and dispatcher
    assert(callback4 == 1 && callback5 == 1);
    assert(Dma::succeeded(flags4) && !Dma::succeeded(flags5));
    assert(Dma::terminal(flags5) && !Dma::terminal(Dma::HalfComplete));
    assert(!Dma::succeeded(Dma::Complete | Dma::Error));
    DMA2.ISR = 0;
    ISR_DMA2_Channel4_5();
    assert(callback4 == 1 && callback5 == 1);
    DMA2.channels[4].CCR = 0xffff;
    Dma::cancel(&DMA2.channels[4]);
    assert(DMA2.channels[4].CCR == 0 && DMA2.IFCR == 0xf0000);
    Dma::deinitChannel(&DMA2,3); Dma::deinitChannel(&DMA2,4);
    assert(!(RCC.AHBENR & 2));
}

static void txOwnership()
{
    TxSchedule tx;
    assert(!tx.ready());
    tx.complete(); assert(!tx.ready()); // stray completion cannot enable TX
    tx.enable(); assert(tx.ready());
    tx.start(); assert(!tx.ready());
    tx.complete(); assert(tx.ready()); // success or error only releases ownership
    tx.start(); tx.disable(); assert(!tx.ready());
    tx.complete(); assert(!tx.ready());
    tx.enable(); assert(tx.ready());
}
int main()
{
    clockSnapshots(); queuePublication(); guardedProducers(); buttonGestures(); dmaRegisters(); txOwnership();
    puts("Clock rollover/clamp, SPSC publication, real button gestures, DMA shared vector, TX ownership passed.");
}
