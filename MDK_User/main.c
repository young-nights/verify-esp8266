#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "bsp_esp01s.h"

extern void delay_ms(uint16_t nms);

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    delay_init(72);
    LED_Init();

    /* LED1 ON = system running */
    LED1_On();

    ESP01S_Init();

    while (1)
    {
        ESP01S_Process();

        /* Send test data every 5 seconds when client connected */
        {
            static uint32_t lastTick = 0;
            extern volatile uint32_t TimeCnt_ms;
            if ((TimeCnt_ms - lastTick) >= 5000) {
                lastTick = TimeCnt_ms;
                ESP01S_SendTestData();
            }
        }
    }
}
