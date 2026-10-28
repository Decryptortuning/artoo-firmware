#include "src/ili9341parallel.h"
#include <assert.h>
#include <stdio.h>
volatile GPIO_t GPIOC;
volatile TIM_t TIM3;
int main() {
    const unsigned inputs[] = {0,30,100,101,0xffffffff};
    for (unsigned percent : inputs) {
        GPIOC.BSRR = 0;
        GPIOC.BRR = 0;
        TIM3.ARR = 1000;
        for (auto &channel : TIM3.compareCapRegs) channel.CCR = 77;
        ILI9341Parallel::lcd.setBacklight(percent);
#if BOARD >= BOARD_DVT
        const unsigned expected = (percent > 100 ? 100 : percent) * 10;
        assert(TIM3.compareCapRegs[3].CCR == expected);
        assert(TIM3.compareCapRegs[0].CCR == 77 && TIM3.compareCapRegs[2].CCR == 77);
        assert(GPIOC.BSRR == 0 && GPIOC.BRR == 0);
#else
        assert(GPIOC.BSRR == (percent ? (1u << 9) : 0));
        assert(GPIOC.BRR == (percent ? 0 : (1u << 9)));
        assert(TIM3.compareCapRegs[3].CCR == 77);
#endif
    }
    puts("Actual LCD backlight setter: board-specific registers and 0/30/100/overflow endpoints passed.");
}
