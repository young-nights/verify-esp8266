#ifndef __BSP_BEEP_H
#define __BSP_BEEP_H
#include "stm32f10x.h"

void BEEP_SetCycleDuty(uint16_t cycle, uint16_t duty);
void BEEP_Blink(uint8_t count, uint8_t mute, uint16_t repeat);

#endif /* __BSP_BEEP_H */
