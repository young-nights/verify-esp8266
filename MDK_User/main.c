#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "bsp_esp01s.h"

extern void delay_ms(uint16_t nms);

/* TIM2 1ms tick timer initialization */
static void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_InitStructure.TIM_Period = 1000 - 1;
    TIM_InitStructure.TIM_Prescaler = 71;
    TIM_InitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_InitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_InitStructure);

    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    delay_init(72);
    Debug_USART_Init();
    TIM2_Init();

    printf("\r\n========================================\r\n");
    printf("  ESP-01S Test Project\r\n");
    printf("  USART2 -> ESP-01S (PA2=TX, PA3=RX)\r\n");
    printf("  USART1 -> Debug (PA9=TX, 115200)\r\n");
    printf("========================================\r\n");

    LED_Init();
    LED1_On();

    printf("[INIT] Starting ESP-01S init...\r\n");
    ESP01S_Init();

    /* Flush leftover AT command responses from init */
    ESP01S_DumpRingBuf();
    ESP01S_FlushRingBuf();
    printf("[INIT] RingBuf flushed. Ready.\r\n");

    while (1)
    {
        ESP01S_Process();

        {
            static uint32_t lastStatusTick = 0;
            static uint32_t lastAutoSendTick = 0;
            static uint8_t prevClientState = 0;
            extern volatile uint32_t TimeCnt_ms;

            /* Detect client state change */
            if (ESP01S_IsClientConnected() != prevClientState) {
                prevClientState = ESP01S_IsClientConnected();
                printf("[STATE] Client changed to: %d\r\n", prevClientState);
            }

            /* Status + CIPSTATUS every 5s, server health check every 10s */
            if ((TimeCnt_ms - lastStatusTick) >= 5000) {
                static uint8_t healthCheckCount = 0;
                lastStatusTick = TimeCnt_ms;
                printf("[STATUS] WiFi=%d Client=%d\r\n",
                    ESP01S_WiFiConnected, ESP01S_IsClientConnected());

                /* Query ESP for actual connection status */
                ESP01S_QueryStatus();

                /* Server health check every 10s (every other cycle) */
                healthCheckCount++;
                if (healthCheckCount >= 2) {
                    healthCheckCount = 0;
                    ESP01S_EnsureServer();
                }
            }

            /* Auto-send every 3s regardless of client state (for testing) */
            if ((TimeCnt_ms - lastAutoSendTick) >= 3000) {
                lastAutoSendTick = TimeCnt_ms;
                if (ESP01S_IsClientConnected()) {
                    printf("[TX] Client connected, sending test data...\r\n");
                    ESP01S_SendTestData();
                    printf("[TX] Send attempt done.\r\n");
                } else {
                    printf("[TX] No client, skip send.\r\n");
                }
            }
        }
    }
}
