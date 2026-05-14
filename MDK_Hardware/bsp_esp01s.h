#ifndef __BSP_ESP01S_H
#define __BSP_ESP01S_H
#include "stm32f10x.h"

extern uint8_t ESP01S_WiFiConnected;  /* WiFi online flag: 0=offline, 1=online */

void ESP01S_Init(void);
void ESP01S_Process(void);            /* Call from main loop to process received data */
void ESP01S_SendSensorData(void);     /* Send CMD 0x01 sensor data frame */
void ESP01S_SendDeviceStatus(void);   /* Send CMD 0x02 device status frame */
void ESP01S_SendAlarm(uint8_t type, uint8_t status); /* Send CMD 0x03 alarm event */

#endif /*__BSP_ESP01S_H*/
