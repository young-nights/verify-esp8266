#include "bsp_led.h"

void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(LED1_GPIO_CLK | LED2_GPIO_CLK, ENABLE);

    /* LED1 PA8 */
    GPIO_InitStructure.GPIO_Pin   = LED1_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED1_GPIO_PORT, &GPIO_InitStructure);

    /* LED2 PB8 */
    GPIO_InitStructure.GPIO_Pin   = LED2_GPIO_PIN;
    GPIO_Init(LED2_GPIO_PORT, &GPIO_InitStructure);

    /* Default: both OFF */
    LED1_Off();
    LED2_Off();
}

void LED1_On(void)      { GPIO_ResetBits(LED1_GPIO_PORT, LED1_GPIO_PIN); }
void LED1_Off(void)     { GPIO_SetBits(LED1_GPIO_PORT, LED1_GPIO_PIN); }
void LED1_Toggle(void)  { GPIO_WriteBit(LED1_GPIO_PORT, LED1_GPIO_PIN,
                            GPIO_ReadOutputDataBit(LED1_GPIO_PORT, LED1_GPIO_PIN) ? Bit_RESET : Bit_SET); }

void LED2_On(void)      { GPIO_ResetBits(LED2_GPIO_PORT, LED2_GPIO_PIN); }
void LED2_Off(void)     { GPIO_SetBits(LED2_GPIO_PORT, LED2_GPIO_PIN); }
void LED2_Toggle(void)  { GPIO_WriteBit(LED2_GPIO_PORT, LED2_GPIO_PIN,
                            GPIO_ReadOutputDataBit(LED2_GPIO_PORT, LED2_GPIO_PIN) ? Bit_RESET : Bit_SET); }
