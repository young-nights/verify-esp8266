#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "bsp_esp01s.h"

extern void delay_ms(uint16_t nms);

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    delay_init(72);
    Debug_USART_Init();

    printf("\r\n========================================\r\n");
    printf("  ESP-01S Test Project\r\n");
    printf("  USART2 -> ESP-01S (PA2=TX, PA3=RX)\r\n");
    printf("  USART1 -> Debug (PA9=TX, 115200)\r\n");
    printf("========================================\r\n");

    LED_Init();
    LED1_On();

    printf("[INIT] Starting ESP-01S init...\r\n");
    ESP01S_Init();
    printf("[INIT] ESP-01S init done.\r\n");

    while (1)
    {
        ESP01S_Process();

        /* Send test data every 5 seconds when client connected */
        {
            static uint32_t lastTick = 0;
            static uint32_t lastStatusTick = 0;
            extern volatile uint32_t TimeCnt_ms;

            /* Periodic status report every 10s */
            if ((TimeCnt_ms - lastStatusTick) >= 10000) {
                lastStatusTick = TimeCnt_ms;
                printf("[STATUS] WiFi=%d Client=%d\r\n",
                    ESP01S_WiFiConnected, ESP01S_IsClientConnected());
            }

            if ((TimeCnt_ms - lastTick) >= 5000) {
                lastTick = TimeCnt_ms;
                if (ESP01S_IsClientConnected()) {
                    printf("[TX] Sending test data...\r\n");
                    ESP01S_SendTestData();
                }
            }
        }
    }
}
