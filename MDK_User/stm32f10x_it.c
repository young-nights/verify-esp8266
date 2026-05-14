#include "stm32f10x_it.h"
#include "sys.h"

volatile uint32_t TimeCnt_ms = 0;

void NMI_Handler(void) {}
void HardFault_Handler(void) { while (1); }
void MemManage_Handler(void) { while (1); }
void BusFault_Handler(void) { while (1); }
void UsageFault_Handler(void) { while (1); }
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) {}

void GENERAL_TIM_2_IRQHandler(void)
{
    static int msCnt = 0;

    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TimeCnt_ms++;

        if (++msCnt >= 60000)
            msCnt = 0;

        TIM_ClearITPendingBit(TIM2, TIM_FLAG_Update);
    }
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_FLAG_Update);
    }
}
