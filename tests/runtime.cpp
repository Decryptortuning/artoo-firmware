#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include "src/heapbounds.h"
#include "src/hostmessage.h"
#include "src/machine.h"
#include "src/textlines.h"
#include "src/rledecoder.h"
#include "src/stm32/hwtimer.h"

volatile TIM_t TIM1, TIM2, TIM3, TIM4, TIM5, TIM6, TIM7, TIM8;
volatile RCC_t RCC;
const IVT_t IVT = {};

static void boundsAndBits()
{
    uintptr_t current = 1000;
    assert(HeapBounds::advance(1000,2000,current,1000) && current == 2000);
    assert(!HeapBounds::advance(1000,2000,current,1) && current == 2000);
    assert(HeapBounds::advance(1000,2000,current,-1000) && current == 1000);
    assert(!HeapBounds::advance(1000,2000,current,-1) && current == 1000);
    assert(!HeapBounds::advance(1000,2000,current,PTRDIFF_MIN));
    assert(!HeapBounds::advance(1000,2000,current,PTRDIFF_MAX));
    for (unsigned shift = 0; shift < 100; ++shift) {
        assert(Intrinsic::ROL(Intrinsic::ROR(0x12345678,shift),shift) == 0x12345678);
        assert(Intrinsic::BitIsSet(UINT32_MAX,shift) == (shift < 32));
    }
    assert(!Intrinsic::BitIsSet(UINT32_MAX,UINT32_MAX));
    assert(Intrinsic::CLZ(0) == 32 && Intrinsic::CTZ(0) == 32);
    assert(Intrinsic::abs(INT32_MIN) == INT32_MAX && Intrinsic::abs(-7) == 7);
}
static void registerHelpers()
{
    HwTimer timer(&TIM1);
    for (int ch = 1; ch <= 3; ++ch) {
        TIM1.CCER = 0;
        timer.enableComplementaryOutput(ch);
        assert(TIM1.CCER == (1u << ((ch-1)*4+2)));
        timer.invertComplementaryOutput(ch);
        assert(TIM1.CCER == (3u << ((ch-1)*4+2)));
        timer.disableComplementaryOutput(ch);
        assert(TIM1.CCER == (1u << ((ch-1)*4+3)));
    }
    TIM1.CCER = 0;
    for (int ch : {-100,0,4,100}) timer.enableComplementaryOutput(ch);
    assert(TIM1.CCER == 0);
    timer.setDuty(0,99); timer.setDuty(5,99);
    assert(timer.lastCapture(0) == 0 && !timer.channelIsEnabled(5));
    HwTimer advanced(&TIM8);
    advanced.init(1000,10);
    assert(RCC.APB2ENR & (1u << 13));
    assert(TIM8.ARR == 1000 && TIM8.PSC == 10 && TIM8.BDTR == (1u << 15));
    advanced.deinit(); assert(!(RCC.APB2ENR & (1u << 13)));
    NVIC_t nvic = {};
    nvic.irqPrioritize(IVT.DMA1_Channel1,0xf0);
    nvic.irqPrioritize(IVT.DMA1_Channel1,0x20);
    assert((nvic.irqPriority[11/4] >> ((11%4)*8)) == 0x20);
    nvic.sysHandlerPrioritize(IVT.SysTick,0xf0);
    nvic.sysHandlerPrioritize(IVT.SysTick,0x10);
    assert(nvic.sysHandlerPriority[2] == 0x10000000);
}
static void textAndRle()
{
    const char *cases[] = {"", "\n", "\n\n", "a\n", "a\nb", "ab\n\ncd\n"};
    const unsigned counts[] = {1,2,3,2,2,4};
    for (unsigned i = 0; i < 6; ++i) {
        std::vector<std::string> lines;
        TextLines::visit(cases[i],[&](const char *text,size_t size) { lines.emplace_back(text,size); });
        assert(lines.size() == counts[i]);
        std::string reconstructed;
        for (unsigned j = 0; j < lines.size(); ++j) {
            if (j) reconstructed += '\n';
            reconstructed += lines[j];
        }
        assert(reconstructed == cases[i]);
    }
    const uint8_t valid[] = {0xff,2,1,3,4,0x80}; // two repeats, then two literals
    unsigned pixels = 0;
    auto output = [&](uint8_t index,unsigned count) { assert(index <= 4); pixels += count; };
    assert(RleDecoder::decode(valid,sizeof valid,4,4,output) && pixels == 4);
    for (unsigned length = 0; length < sizeof valid; ++length) {
        pixels = 0;
        assert(!RleDecoder::decode(valid,length,4,4,output));
        assert(pixels <= 4);
    }
    pixels = 0; assert(!RleDecoder::decode(valid,sizeof valid,3,4,output));
    pixels = 0; assert(!RleDecoder::decode(valid,sizeof valid,4,1,output));
    uint32_t random = 42;
    uint8_t data[32];
    for (unsigned trial = 0; trial < 100000; ++trial) {
        for (auto &byte : data) { random = random*1664525u+1013904223u; byte = random >> 24; }
        pixels = 0;
        RleDecoder::decode(data,trial%33,32,4,output);
        assert(pixels <= 32);
    }
}
int main()
{
    boundsAndBits(); registerHelpers(); textAndRle();
    assert(HostMessage::requestEnabled(HostMessage::SetRawIo));
    for (unsigned id : {unsigned(HostMessage::OutputTest),unsigned(HostMessage::GpioTest),
                       unsigned(HostMessage::SelfTest),unsigned(HostMessage::TestEvent)})
        assert(HostMessage::requestEnabled(id) == bool(ARTOO_FACTORY_TESTS));
    puts("Heap limits, actual timer/NVIC helpers, shifts, text/RLE bounds and factory gates passed.");
}
