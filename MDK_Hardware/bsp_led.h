#ifndef __BSP_LED_H
#define __BSP_LED_H
#include "stm32f10x.h"

/* LED1 - PA8 */
#define LED1_GPIO_CLK    RCC_APB2Periph_GPIOA
#define LED1_GPIO_PORT   GPIOA
#define LED1_GPIO_PIN    GPIO_Pin_8

/* LED2 - PB8 */
#define LED2_GPIO_CLK    RCC_APB2Periph_GPIOB
#define LED2_GPIO_PORT   GPIOB
#define LED2_GPIO_PIN    GPIO_Pin_8

void LED_Init(void);
void LED1_On(void);
void LED1_Off(void);
void LED1_Toggle(void);
void LED2_On(void);
void LED2_Off(void);
void LED2_Toggle(void);

#endif /* __BSP_LED_H */
