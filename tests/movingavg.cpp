#include "src/movingavgfilter.h"
#include "src/paramvalidation.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdint.h>

static int32_t reference(int32_t previous,int32_t sample,uint16_t alpha) {
    if (alpha==USHRT_MAX) return sample;
    // Wider host oracle preserves the historical signed rounding convention.
    const __int128 weighted = __int128(previous)*(65535-alpha) + __int128(sample)*(alpha+1);
    return int32_t((weighted+32768)/65536);
}
int main() {
    MovingAvgFilter battery, camera;
    const int32_t batteryGolden[] = {10,19,27,34,41};
    const int32_t cameraGolden[] = {63,82,88,89,90};
    for (unsigned i=0;i<5;++i) {
        battery.update(100,MovingAvgFilter::Alpha(0.1f));
        camera.update(90,MovingAvgFilter::Alpha(0.7f));
        assert(battery.average()==batteryGolden[i] && camera.average()==cameraGolden[i]);
    }
    const int32_t values[] = {INT32_MIN,INT32_MIN+1,-100,-1,0,1,100,4294966,INT32_MAX};
    const uint16_t alphas[] = {0,1,6553,32767,45874,65534,65535};
    MovingAvgFilter filter;
    for (int32_t previous : values) for (int32_t sample : values) for (uint16_t alpha : alphas) {
        filter.update(previous,USHRT_MAX);
        assert(filter.average()==previous);
        filter.update(sample,alpha);
        assert(filter.average()==reference(previous,sample,alpha));
    }
    assert(ParamValidation::sweep(1,4294966));
    filter.update(4294966,USHRT_MAX);
    filter.update(4294966,MovingAvgFilter::Alpha(0.7f));
    assert(filter.average()==4294966);
    uint32_t random=42;
    int32_t previous=0;
    filter.update(previous,USHRT_MAX);
    for (unsigned i=0;i<100000;++i) {
        random=random*1664525u+1013904223u;
        const int32_t sample=int32_t(random);
        random=random*1664525u+1013904223u;
        const uint16_t alpha=random>>16;
        filter.update(sample,alpha);
        previous=reference(previous,sample,alpha);
        assert(filter.average()==previous);
    }
    puts("Actual moving-average filter: nominal golden sequences, valid large sweep, full signed boundaries and 100k steps passed.");
}
