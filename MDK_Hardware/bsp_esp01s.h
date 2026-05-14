#ifndef __BSP_ESP01S_H
#define __BSP_ESP01S_H
#include "stm32f10x.h"

extern uint8_t ESP01S_WiFiConnected;

void ESP01S_Init(void);
void ESP01S_Process(void);
void ESP01S_SendTestData(void);  /* Send a test frame with dummy data to verify connectivity */

#endif /* __BSP_ESP01S_H */
