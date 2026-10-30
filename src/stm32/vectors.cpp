
#include "vectors.h"

extern unsigned _stack;

// No C prologue touches the potentially damaged exception stack. Reentry halts
// before using the emergency stack again. Ordinary IRQs now capture, too.
#define FAULT_HANDLER(name) \
    NAKED_HANDLER name() { \
        __asm__ volatile( \
            "cpsid i\n" \
            "tst lr, #4\n" \
            "ite eq\n" \
            "mrseq r0, msp\n" \
            "mrsne r0, psp\n" \
            "mov r1, lr\n" \
            "mrs r2, ipsr\n" \
            "ldr r3, =artoo_fault_active\n" \
            "ldr r12, [r3]\n" \
            "cmp r12, #0\n" \
            "bne 1f\n" \
            "movs r4, #1\n" \
            "str r4, [r3]\n" \
            "ldr r3, =__fault_stack_end\n" \
            "msr msp, r3\n" \
            "b artoo_capture_fault\n" \
            "1: b 1b\n"); \
    }
FAULT_HANDLER(ISR_Default)
FAULT_HANDLER(ISR_DefaultNMI)
FAULT_HANDLER(ISR_DefaultHardFault)
FAULT_HANDLER(ISR_DefaultMemManage)
FAULT_HANDLER(ISR_DefaultBusFault)
FAULT_HANDLER(ISR_DefaultUsageFault)
FAULT_HANDLER(ISR_DefaultSVCall)
FAULT_HANDLER(ISR_DefaultDebug)

#pragma weak ISR_NMI                = ISR_DefaultNMI
#pragma weak ISR_HardFault          = ISR_DefaultHardFault
#pragma weak ISR_MemManage          = ISR_DefaultMemManage
#pragma weak ISR_BusFault           = ISR_DefaultBusFault
#pragma weak ISR_UsageFault         = ISR_DefaultUsageFault
#pragma weak ISR_SVCall             = ISR_DefaultSVCall
#pragma weak ISR_Debug              = ISR_DefaultDebug
#pragma weak ISR_PendSV             = ISR_Default
#pragma weak ISR_SysTick            = ISR_Default
#pragma weak ISR_WWDG               = ISR_Default
#pragma weak ISR_PVD                = ISR_Default
#pragma weak ISR_Tamper             = ISR_Default
#pragma weak ISR_RTC                = ISR_Default
#pragma weak ISR_Flash              = ISR_Default
#pragma weak ISR_RCC                = ISR_Default
#pragma weak ISR_EXTI0              = ISR_Default
#pragma weak ISR_EXTI1              = ISR_Default
#pragma weak ISR_EXTI2              = ISR_Default
#pragma weak ISR_EXTI3              = ISR_Default
#pragma weak ISR_EXTI4              = ISR_Default
#pragma weak ISR_DMA1_Channel1      = ISR_Default
#pragma weak ISR_DMA1_Channel2      = ISR_Default
#pragma weak ISR_DMA1_Channel3      = ISR_Default
#pragma weak ISR_DMA1_Channel4      = ISR_Default
#pragma weak ISR_DMA1_Channel5      = ISR_Default
#pragma weak ISR_DMA1_Channel6      = ISR_Default
#pragma weak ISR_DMA1_Channel7      = ISR_Default
#pragma weak ISR_ADC1_2             = ISR_Default
#pragma weak ISR_USB_HP_CAN_TX      = ISR_Default
#pragma weak ISR_USB_LP_CAN_RX0     = ISR_Default
#pragma weak ISR_CAN_RX1            = ISR_Default
#pragma weak ISR_CAN_SCE            = ISR_Default
#pragma weak ISR_EXTI9_5            = ISR_Default
#pragma weak ISR_TIM1_BRK           = ISR_Default
#pragma weak ISR_TIM1_UP            = ISR_Default
#pragma weak ISR_TIM1_TRG_COM       = ISR_Default
#pragma weak ISR_TIM1_CC            = ISR_Default
#pragma weak ISR_TIM2               = ISR_Default
#pragma weak ISR_TIM3               = ISR_Default
#pragma weak ISR_TIM4               = ISR_Default
#pragma weak ISR_I2C1_EV            = ISR_Default
#pragma weak ISR_I2C1_ER            = ISR_Default
#pragma weak ISR_I2C2_EV            = ISR_Default
#pragma weak ISR_I2C2_ER            = ISR_Default
#pragma weak ISR_SPI1               = ISR_Default
#pragma weak ISR_SPI2               = ISR_Default
#pragma weak ISR_USART1             = ISR_Default
#pragma weak ISR_USART2             = ISR_Default
#pragma weak ISR_USART3             = ISR_Default
#pragma weak ISR_EXTI15_10          = ISR_Default
#pragma weak ISR_RTCAlarm           = ISR_Default
#pragma weak ISR_UsbWakeup          = ISR_Default
#pragma weak ISR_TIM8_BRK           = ISR_Default
#pragma weak ISR_TIM8_UP            = ISR_Default
#pragma weak ISR_TIM8_TRG_COM       = ISR_Default
#pragma weak ISR_TIM8_CC            = ISR_Default
#pragma weak ISR_ADC3               = ISR_Default
#pragma weak ISR_FSMC               = ISR_Default
#pragma weak ISR_SDIO               = ISR_Default
#pragma weak ISR_TIM5               = ISR_Default
#pragma weak ISR_SPI3               = ISR_Default
#pragma weak ISR_UART4              = ISR_Default
#pragma weak ISR_UART5              = ISR_Default
#pragma weak ISR_TIM6               = ISR_Default
#pragma weak ISR_TIM7               = ISR_Default
#pragma weak ISR_DMA2_Channel1      = ISR_Default
#pragma weak ISR_DMA2_Channel2      = ISR_Default
#pragma weak ISR_DMA2_Channel3      = ISR_Default
#pragma weak ISR_DMA2_Channel4_5    = ISR_Default

__attribute__ ((section (".vectors"))) const struct IVT_t IVT =
{
    &_stack,
    _start,
    ISR_NMI,
    ISR_HardFault,
    ISR_MemManage,
    ISR_BusFault,
    ISR_UsageFault,
    { 0 },
    ISR_SVCall,
    ISR_Debug,
    0,
    ISR_PendSV,
    ISR_SysTick,
    ISR_WWDG,
    ISR_PVD,
    ISR_Tamper,
    ISR_RTC,
    ISR_Flash,
    ISR_RCC,
    ISR_EXTI0,
    ISR_EXTI1,
    ISR_EXTI2,
    ISR_EXTI3,
    ISR_EXTI4,
    ISR_DMA1_Channel1,
    ISR_DMA1_Channel2,
    ISR_DMA1_Channel3,
    ISR_DMA1_Channel4,
    ISR_DMA1_Channel5,
    ISR_DMA1_Channel6,
    ISR_DMA1_Channel7,
    ISR_ADC1_2,
    ISR_USB_HP_CAN_TX,
    ISR_USB_LP_CAN_RX0,
    ISR_CAN_RX1,
    ISR_CAN_SCE,
    ISR_EXTI9_5,
    ISR_TIM1_BRK,
    ISR_TIM1_UP,
    ISR_TIM1_TRG_COM,
    ISR_TIM1_CC,
    ISR_TIM2,
    ISR_TIM3,
    ISR_TIM4,
    ISR_I2C1_EV,
    ISR_I2C1_ER,
    ISR_I2C2_EV,
    ISR_I2C2_ER,
    ISR_SPI1,
    ISR_SPI2,
    ISR_USART1,
    ISR_USART2,
    ISR_USART3,
    ISR_EXTI15_10,
    ISR_RTCAlarm,
    ISR_UsbWakeup,
    ISR_TIM8_BRK,
    ISR_TIM8_UP,
    ISR_TIM8_TRG_COM,
    ISR_TIM8_CC,
    ISR_ADC3,
    ISR_FSMC,
    ISR_SDIO,
    ISR_TIM5,
    ISR_SPI3,
    ISR_UART4,
    ISR_UART5,
    ISR_TIM6,
    ISR_TIM7,
    ISR_DMA2_Channel1,
    ISR_DMA2_Channel2,
    ISR_DMA2_Channel3,
    ISR_DMA2_Channel4_5,
};
