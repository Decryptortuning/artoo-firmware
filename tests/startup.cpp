#include "src/startupfailure.h"
#include "src/stm32/adc.h"
#include "src/stm32/dma.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

volatile RCC_t RCC;
volatile ADC_t ADC1, ADC2, ADC3;
volatile DMA_t DMA1, DMA2;
static volatile DMAChannel_t channel;
static bool noDma;
static unsigned reads, readyAt;
static uint32_t stuckBits;
static volatile uint32_t *scriptRegister;
static uint32_t targetValue;

uint32_t StartupFailure::readRegister(volatile uint32_t &reg) {
    ++reads;
    if (&reg == scriptRegister && reads >= readyAt) reg = targetValue;
    if (&reg == &ADC1.CR2) reg &= ~(uint32_t(12) & ~stuckBits);
    return reg;
}
volatile DMAChannel_t *Dma::initChannel(volatile DMA_t *, int, DmaIsr_t, void *) {
    return noDma ? NULL : &channel;
}
static void reset() {
    memset(const_cast<RCC_t*>(&RCC),0,sizeof RCC);
    memset(const_cast<ADC_t*>(&ADC1),0,sizeof ADC1);
    memset(const_cast<ADC_t*>(&ADC2),0,sizeof ADC2);
    memset(const_cast<ADC_t*>(&ADC3),0,sizeof ADC3);
    memset(const_cast<DMAChannel_t*>(&channel),0,sizeof channel);
    reads = 0; readyAt = 0; scriptRegister = NULL; stuckBits = 0; noDma = false;
}
int main() {
    volatile uint32_t reg = 0;
    scriptRegister = &reg; targetValue = 0x85; readyAt = 3;
    assert(StartupFailure::waitMasked(reg, 0x80, 0x80, 3));
    assert(reads == 3); // Success exactly on last permitted observation.
    reads = 0; reg = 0; readyAt = 4;
    assert(!StartupFailure::waitMasked(reg, 0x80, 0x80, 3));
    assert(reads == 3);
    reads = 0;
    assert(!StartupFailure::waitMasked(reg, 0, 0, 0));
    assert(reads == 0);
    reg = 0x85; scriptRegister = NULL;
    assert(StartupFailure::waitMasked(reg, 0x80, 0x80, 1));
    const int64_t start = 999;
    assert(!StartupFailure::batteryExpired(false,start,start+StartupFailure::BatteryWaitNs-1));
    assert(StartupFailure::batteryExpired(false,start,start+StartupFailure::BatteryWaitNs));
    assert(!StartupFailure::batteryExpired(true,start,start+StartupFailure::BatteryWaitNs));
    assert(!StartupFailure::batteryExpired(true,start,start+StartupFailure::BatteryWaitNs+1));

    reset();
    Adc adc(&ADC1,NULL);
    uint16_t samples[2] = {};
    adc.beginSequence(2,samples);
    assert(channel.CCR == 0); // Cannot start before successful init.
    noDma = true;
    assert(adc.init(true) == StartupFailure::AdcDma);
    adc.beginSequence(2,samples); assert(channel.CCR == 0 && ADC1.CR2 == 0);
    reset(); stuckBits = 8;
    assert(adc.init(true) == StartupFailure::AdcResetCalibration);
    adc.beginSequence(2,samples); assert(channel.CCR == 0 && ADC1.CR2 == 0);
    reset(); stuckBits = 4;
    assert(adc.init(true) == StartupFailure::AdcCalibration);
    adc.beginSequence(2,samples); assert(channel.CCR == 0 && ADC1.CR2 == 0);
    reset();
    assert(adc.init(true) == StartupFailure::None);
    adc.beginSequence(2,samples);
    assert(channel.CNDTR == 2 && (channel.CCR & 1) && (ADC1.CR2 & (1u<<22)));
    // Unused synchronous API never overwrites caller's value on timeout.
    uint16_t sample = 0x55aa;
    assert(!adc.sampleSync(0,sample) && sample == 0x55aa);
    ADC1.SR = 2; ADC1.DR = 0;
    assert(adc.sampleSync(0,sample) && sample == 0); // Zero is a valid sample.
    puts("Bounded register polling, battery deadline and actual ADC failure/DMA guard pass");
}
