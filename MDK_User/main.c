#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "bsp_esp01s.h"

int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
    delay_init(72);

    /* Initialize ESP-01S WiFi module (AP mode + TCP server) */
    ESP01S_Init();

    while (1)
    {
        ESP01S_Process();
    }
}
