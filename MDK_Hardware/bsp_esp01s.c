/**
 * @file    bsp_esp01s.c
 * @brief   ESP-01S WiFi module driver (AP mode + TCP server + custom protocol)
 *
 * Hardware: ESP-01S on USART2 (PA2=TX, PA3=RX), 115200 baud
 * Protocol: 0xAA | CMD | LEN | PAYLOAD | CHECKSUM | 0x55
 * TCP port: 8080
 */

#include "bsp_esp01s.h"
#include <stdio.h>
#include <string.h>

/* ======================== Configuration ======================== */
#define ESP01S_USART            USART2
#define ESP01S_BAUDRATE         115200
#define ESP01S_RINGBUF_SIZE     512
#define ESP01S_MAX_LINKS        5       /* CIPMUX=1 supports up to 5 links */
#define ESP01S_TX_BUF_SIZE      128     /* AT+CIPSEND staging buffer */
#define ESP01S_AT_TIMEOUT_MS    3000

/* Hardware reset pins */
#define ESP01S_RST_PORT         GPIOA
#define ESP01S_RST_PIN          GPIO_Pin_4
#define ESP01S_EN_PORT          GPIOA
#define ESP01S_EN_PIN           GPIO_Pin_1

/* Frame constants */
#define FRAME_HEADER            0xAA
#define FRAME_END               0x55
#define FRAME_OVERHEAD          5       /* HEAD+CMD+LEN+SUM+END */

/* TX commands (MCU -> APP) */
#define CMD_TEST_DATA           0x01    /* Test data frame */

/* RX commands (APP -> MCU) */
#define CMD_ECHO_REQUEST        0x10    /* APP sends echo, MCU responds */

/* ======================== Ring Buffer ======================== */
static volatile uint8_t  s_ringBuf[ESP01S_RINGBUF_SIZE];
static volatile uint16_t s_ringHead = 0;   /* Write index (ISR) */
static volatile uint16_t s_ringTail = 0;   /* Read index (main) */

/* ======================== State ======================== */
uint8_t ESP01S_WiFiConnected = 0;
static uint8_t s_clientLinkId = 0;         /* Connected client link ID */
static uint8_t s_clientConnected = 0;      /* 0=no client, 1=client connected */
static volatile uint32_t s_connectCount = 0;   /* Debug: total connections received */
static volatile uint32_t s_disconnectCount = 0; /* Debug: total disconnections */
static volatile uint32_t s_ipdCount = 0;        /* Debug: total +IPD received */

/* ======================== USART2 Init ======================== */
static void ESP01S_USART_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* Enable clocks: GPIOA on APB2, USART2 on APB1 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* PA4: ESP-01S RST - Output push-pull, default HIGH */
    GPIO_InitStructure.GPIO_Pin   = ESP01S_RST_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP01S_RST_PORT, &GPIO_InitStructure);
    GPIO_SetBits(ESP01S_RST_PORT, ESP01S_RST_PIN);  /* RST idle HIGH */

    /* PA1: ESP-01S EN - Output push-pull, HIGH to enable */
    GPIO_InitStructure.GPIO_Pin   = ESP01S_EN_PIN;
    GPIO_Init(ESP01S_EN_PORT, &GPIO_InitStructure);
    GPIO_SetBits(ESP01S_EN_PORT, ESP01S_EN_PIN);    /* EN HIGH = module ON */

    /* PA2: TX - Alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA3: RX - Floating input */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART2 configuration */
    USART_InitStructure.USART_BaudRate            = ESP01S_BAUDRATE;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(ESP01S_USART, &USART_InitStructure);

    /* NVIC: USART2 interrupt */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_ITConfig(ESP01S_USART, USART_IT_RXNE, ENABLE);
    USART_Cmd(ESP01S_USART, ENABLE);
}

/* ======================== USART2 IRQ Handler ======================== */
/**
 * @brief  USART2 interrupt handler - push received bytes into ring buffer.
 *         Also monitors for WiFi connect/disconnect and +IPD patterns
 *         by appending bytes into the ring buffer for main-loop parsing.
 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(ESP01S_USART, USART_IT_RXNE) != RESET) {
        uint8_t ch = (uint8_t)USART_ReceiveData(ESP01S_USART);
        uint16_t next = (s_ringHead + 1) % ESP01S_RINGBUF_SIZE;
        if (next != s_ringTail) {  /* Buffer not full */
            s_ringBuf[s_ringHead] = ch;
            s_ringHead = next;
        }
        USART_ClearITPendingBit(ESP01S_USART, USART_IT_RXNE);
    }
}

/* ======================== Low-level helpers ======================== */

/** Wait for USART TX register empty, then send one byte */
static void USART_SendByte(uint8_t ch)
{
    while (USART_GetFlagStatus(ESP01S_USART, USART_FLAG_TXE) == RESET);
    USART_SendData(ESP01S_USART, ch);
}

/** Send a C string via USART2 (blocking) */
static void USART_SendString(const char* str)
{
    while (*str) {
        USART_SendByte((uint8_t)*str++);
    }
    while (USART_GetFlagStatus(ESP01S_USART, USART_FLAG_TC) == RESET);
}

/** Send a raw byte array via USART2 (blocking) */
static void USART_SendBytes(const uint8_t* data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        USART_SendByte(data[i]);
    }
    while (USART_GetFlagStatus(ESP01S_USART, USART_FLAG_TC) == RESET);
}

/** Check if ring buffer contains a specific string */
static int RingBuf_Contains(const char* keyword)
{
    uint16_t len = strlen(keyword);
    uint16_t count, i;

    /* Calculate available bytes */
    if (s_ringHead >= s_ringTail)
        count = s_ringHead - s_ringTail;
    else
        count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;

    if (count < len) return 0;

    /* Linear scan through ring buffer */
    for (i = 0; i <= count - len; i++) {
        uint16_t idx = (s_ringTail + i) % ESP01S_RINGBUF_SIZE;
        uint16_t j;
        int match = 1;
        for (j = 0; j < len; j++) {
            if (s_ringBuf[(idx + j) % ESP01S_RINGBUF_SIZE] != (uint8_t)keyword[j]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/** Discard bytes from ring buffer up to and including 'keyword', return 1 if found */
static int RingBuf_SkipUntil(const char* keyword)
{
    uint16_t len = strlen(keyword);
    uint16_t count, i;

    if (s_ringHead >= s_ringTail)
        count = s_ringHead - s_ringTail;
    else
        count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;

    if (count < len) return 0;

    for (i = 0; i <= count - len; i++) {
        uint16_t idx = (s_ringTail + i) % ESP01S_RINGBUF_SIZE;
        uint16_t j;
        int match = 1;
        for (j = 0; j < len; j++) {
            if (s_ringBuf[(idx + j) % ESP01S_RINGBUF_SIZE] != (uint8_t)keyword[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            /* Advance tail past the keyword */
            s_ringTail = (idx + len) % ESP01S_RINGBUF_SIZE;
            return 1;
        }
    }
    return 0;
}

/** Flush ring buffer */
static void RingBuf_Flush(void)
{
    s_ringTail = s_ringHead;
}

/** Millisecond delay using the delay_init-based delay_ms */
extern void delay_ms(uint16_t nms);

/* ======================== AT Command Engine ======================== */

/**
 * @brief  Send AT command and wait for expected response.
 * @param  cmd: AT command string (without \r\n)
 * @param  ack: Expected acknowledgement string (e.g. "OK", "SEND OK")
 * @param  timeout_ms: Timeout in milliseconds
 * @retval 1=success, 0=timeout
 */
static uint8_t ESP01S_SendCmd(const char* cmd, const char* ack, uint16_t timeout_ms)
{
    uint16_t elapsed = 0;

    /* Flush stale data */
    RingBuf_Flush();

    /* Send command with \r\n */
    USART_SendString(cmd);
    USART_SendString("\r\n");

    /* Poll for ack */
    while (elapsed < timeout_ms) {
        delay_ms(50);
        elapsed += 50;

        /* Check for expected ack */
        if (RingBuf_Contains(ack)) {
            return 1;
        }

        /* Check for ERROR response */
        if (RingBuf_Contains("ERROR") || RingBuf_Contains("FAIL")) {
            return 0;
        }
    }

    return 0;  /* Timeout */
}

/**
 * @brief  Send AT command, wait for ack, ignore "ALREADY CONNECTED" noise.
 * @retval 1=success, 0=fail
 */
static uint8_t AT_WaitOK(const char* cmd, uint16_t timeout)
{
    if (ESP01S_SendCmd(cmd, "OK", timeout)) return 1;
    if (ESP01S_SendCmd(cmd, "no change", 500)) return 1;
    return 0;
}

/* ======================== Initialization Sequence ======================== */

static void ESP01S_ConfigSequence(void)
{
    uint8_t ok;

    printf("[CFG] Step1: Hardware reset...\r\n");
    GPIO_ResetBits(ESP01S_RST_PORT, ESP01S_RST_PIN);
    delay_ms(100);
    GPIO_SetBits(ESP01S_RST_PORT, ESP01S_RST_PIN);
    delay_ms(1000);
    printf("[CFG] Step1: Done.\r\n");

    printf("[CFG] Step2: AT check...\r\n");
    ok = AT_WaitOK("AT", 2000);
    if (!ok) {
        printf("[CFG] Step2: Retry...\r\n");
        delay_ms(1000);
        ok = AT_WaitOK("AT", 2000);
    }
    printf("[CFG] Step2: %s\r\n", ok ? "OK" : "FAIL");

    printf("[CFG] Step3: Set AP mode...\r\n");
    ok = AT_WaitOK("AT+CWMODE=2", 2000);
    printf("[CFG] Step3: %s\r\n", ok ? "OK" : "FAIL");

    printf("[CFG] Step4: Create AP...\r\n");
    ok = AT_WaitOK("AT+CWSAP=\"ESP8266\",\"12345678\",1,3", 3000);
    delay_ms(1000);
    printf("[CFG] Step4: %s\r\n", ok ? "OK" : "FAIL");

    printf("[CFG] Step5: Enable MUX...\r\n");
    ok = AT_WaitOK("AT+CIPMUX=1", 2000);
    printf("[CFG] Step5: %s\r\n", ok ? "OK" : "FAIL");

    printf("[CFG] Step6: Start TCP server...\r\n");
    ok = AT_WaitOK("AT+CIPSERVER=1,8080", 2000);
    if (!ok) {
        printf("[CFG] Step6: Retry...\r\n");
        delay_ms(1000);
        ok = AT_WaitOK("AT+CIPSERVER=1,8080", 2000);
    }
    printf("[CFG] Step6: %s\r\n", ok ? "OK" : "FAIL");

    printf("[CFG] Step7: Set timeout...\r\n");
    ok = AT_WaitOK("AT+CIPSTO=120", 2000);
    printf("[CFG] Step7: %s\r\n", ok ? "OK" : "FAIL");

    printf("[CFG] Step8: Check IP...\r\n");
    ESP01S_SendCmd("AT+CIFSR", "192.168.4.1", 2000);
    printf("[CFG] Init complete.\r\n");
}

/* ======================== Frame TX ======================== */

/**
 * @brief  Build a protocol frame and send it to a connected client.
 * @param  link_id: TCP connection link ID
 * @param  cmd: Command byte
 * @param  data: Payload data
 * @param  len: Payload length
 * @retval None
 */
static void ESP01S_SendFrame(uint8_t link_id, uint8_t cmd, const uint8_t* data, uint8_t len)
{
    uint8_t frame[ESP01S_TX_BUF_SIZE];
    uint8_t i;
    uint8_t checksum = 0;
    uint16_t frameLen;
    char cipSendCmd[32];

    if (!s_clientConnected) {
        printf("[SEND] No client connected, skip.\r\n");
        return;
    }

    /* Build frame: HEADER | CMD | LEN | PAYLOAD | CHECKSUM | END */
    frameLen = (uint16_t)len + FRAME_OVERHEAD;
    if (frameLen > ESP01S_TX_BUF_SIZE) return;

    frame[0] = FRAME_HEADER;
    frame[1] = cmd;
    frame[2] = len;
    checksum = cmd + len;
    for (i = 0; i < len; i++) {
        frame[3 + i] = data[i];
        checksum += data[i];
    }
    frame[3 + len] = checksum;
    frame[4 + len] = FRAME_END;

    /* AT+CIPSEND=<link_id>,<length> */
    sprintf(cipSendCmd, "AT+CIPSEND=%d,%d", link_id, (int)frameLen);
    printf("[SEND] CMD: %s\r\n", cipSendCmd);
    if (!ESP01S_SendCmd(cipSendCmd, ">", 2000)) {
        printf("[SEND] First attempt failed, retry...\r\n");
        delay_ms(100);
        if (!ESP01S_SendCmd(cipSendCmd, ">", 2000)) {
            printf("[SEND] FAILED - no > prompt\r\n");
            return;
        }
    }
    printf("[SEND] Got > prompt, sending frame...\r\n");

    /* Send raw frame data */
    USART_SendBytes(frame, frameLen);

    /* Wait for SEND OK (short timeout, data is small) */
    {
        uint16_t elapsed = 0;
        while (elapsed < 500) {
            delay_ms(10);
            elapsed += 10;
            if (RingBuf_Contains("SEND OK")) {
                printf("[SEND] SEND OK confirmed\r\n");
                RingBuf_Flush();
                return;
            }
        }
        printf("[SEND] Timeout waiting for SEND OK\r\n");
        RingBuf_Flush();
    }
}

/* ======================== Public TX Functions ======================== */

/**
 * @brief  Send a test frame with 4 bytes of dummy data to verify connectivity.
 */
void ESP01S_SendTestData(void)
{
    uint8_t payload[4] = {0x01, 0x02, 0x03, 0x04};
    if (!s_clientConnected) return;
    ESP01S_SendFrame(s_clientLinkId, CMD_TEST_DATA, payload, 4);
    printf("[TX] Test frame sent (4 bytes)\r\n");
}

uint8_t ESP01S_IsClientConnected(void)
{
    return s_clientConnected;
}

/** Debug: dump ring buffer contents as hex */
void ESP01S_DumpRingBuf(void)
{
    uint16_t count, i;

    if (s_ringHead >= s_ringTail)
        count = s_ringHead - s_ringTail;
    else
        count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;

    if (count == 0) {
        printf("[DUMP] RingBuf empty\r\n");
        return;
    }

    printf("[DUMP] RingBuf %d bytes: ", count);
    for (i = 0; i < count && i < 64; i++) {
        uint8_t b = s_ringBuf[(s_ringTail + i) % ESP01S_RINGBUF_SIZE];
        printf("%02X ", b);
    }
    if (count > 64) printf("...");
    printf("\r\n");

    /* Also print as ASCII for readable chars */
    printf("[DUMP] ASCII: ");
    for (i = 0; i < count && i < 64; i++) {
        uint8_t b = s_ringBuf[(s_ringTail + i) % ESP01S_RINGBUF_SIZE];
        printf("%c", (b >= 0x20 && b < 0x7F) ? b : '.');
    }
    if (count > 64) printf("...");
    printf("\r\n");
}

void ESP01S_FlushRingBuf(void)
{
    s_ringTail = s_ringHead;
}

/* ======================== Frame RX Parsing ======================== */

/**
 * @brief  Handle a received protocol frame.
 * @param  frame: Pointer to frame bytes (includes HEADER..END)
 */
static void ESP01S_HandleFrame(const uint8_t* frame)
{
    uint8_t cmd  = frame[1];
    uint8_t len  = frame[2];
    const uint8_t* payload = &frame[3];

    switch (cmd) {
    case CMD_ECHO_REQUEST:
        /* Echo back the received payload */
        ESP01S_SendFrame(s_clientLinkId, CMD_ECHO_REQUEST, payload, len);
        break;
    default:
        printf("[RX] Unknown CMD=0x%02X LEN=%d\r\n", cmd, len);
        break;
    }
}

/**
 * @brief  Extract and process protocol frames from a data buffer.
 * @param  data: Raw data bytes (may contain +IPD prefix, etc.)
 * @param  dataLen: Number of bytes in data
 */
static void ESP01S_ParseFrames(const uint8_t* data, uint16_t dataLen)
{
    uint16_t i = 0;

    while (i < dataLen) {
        /* Search for frame header */
        if (data[i] != FRAME_HEADER) {
            i++;
            continue;
        }

        /* Need at least HEAD + CMD + LEN = 3 bytes */
        if (i + 3 > dataLen) break;

        {
            uint8_t payloadLen = data[i + 2];
            uint16_t frameSize = (uint16_t)payloadLen + FRAME_OVERHEAD;

            /* Check if we have a complete frame */
            if (i + frameSize > dataLen) {
                i++;
                continue;
            }

            /* Verify frame end */
            if (data[i + frameSize - 1] != FRAME_END) {
                i++;
                continue;
            }

            /* Verify checksum */
            {
                uint8_t calcSum = 0;
                uint8_t j;
                for (j = 1; j < 3 + payloadLen; j++) {
                    calcSum += data[i + j];
                }
                if (calcSum != data[i + frameSize - 2]) {
                    i++;
                    continue;
                }
            }

            /* Valid frame found - handle it */
            ESP01S_HandleFrame(&data[i]);
            i += frameSize;
        }
    }
}

/* ======================== +IPD Parser ======================== */

/**
 * State machine for parsing +IPD,<link_id>,<length>:<data> from ring buffer.
 * Called from ESP01S_Process().
 *
 * Strategy: scan the ring buffer for "+IPD", parse the header,
 * extract the specified number of bytes as the data payload,
 * then call ESP01S_ParseFrames on that payload.
 */

/* Simple decimal parser: read digits from ring buffer at given index */
static int16_t RingBuf_ParseNumber(uint16_t startIdx, uint16_t* outEndIdx, uint16_t maxLen)
{
    int16_t value = 0;
    uint16_t idx = startIdx;
    uint16_t count = 0;
    uint8_t ch;

    while (count < maxLen) {
        ch = s_ringBuf[idx % ESP01S_RINGBUF_SIZE];
        if (ch >= '0' && ch <= '9') {
            value = value * 10 + (ch - '0');
            idx = (idx + 1) % ESP01S_RINGBUF_SIZE;
            count++;
        } else {
            break;
        }
    }

    if (count == 0) return -1;
    *outEndIdx = idx;
    return value;
}

/**
 * @brief  Process ring buffer: look for +IPD, CONNECT, DISCONNECT patterns.
 */
void ESP01S_Process(void)
{
    uint16_t count, i;

    /* Calculate available bytes */
    if (s_ringHead >= s_ringTail)
        count = s_ringHead - s_ringTail;
    else
        count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;

    if (count == 0) return;

    /* ---- Check for WiFi CONNECT/DISCONNECT notifications ---- */
    {
        /* Scan the ring buffer for "WIFI CONNECTED" or "WIFI DISCONNECTED" */
        /* We scan a linear copy to simplify string searching */
        uint16_t scanCount = count < 64 ? count : 64;
        uint8_t scanBuf[64];
        for (i = 0; i < scanCount; i++) {
            scanBuf[i] = s_ringBuf[(s_ringTail + i) % ESP01S_RINGBUF_SIZE];
        }

        /* Check for WIFI CONNECTED */
        for (i = 0; i + 14 <= scanCount; i++) {
            if (memcmp(&scanBuf[i], "WIFI CONNECTED", 14) == 0) {
                ESP01S_WiFiConnected = 1;
                /* Skip past this line */
                RingBuf_SkipUntil("WIFI CONNECTED");
                /* Recalculate count */
                if (s_ringHead >= s_ringTail)
                    count = s_ringHead - s_ringTail;
                else
                    count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;
                break;
            }
        }

        /* Check for WIFI DISCONNECTED */
        for (i = 0; i + 17 <= scanCount; i++) {
            if (memcmp(&scanBuf[i], "WIFI DISCONNECTED", 17) == 0) {
                ESP01S_WiFiConnected = 0;
                s_clientConnected = 0;
                RingBuf_SkipUntil("WIFI DISCONNECTED");
                if (s_ringHead >= s_ringTail)
                    count = s_ringHead - s_ringTail;
                else
                    count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;
                break;
            }
        }
    }

    /* ---- Check for CONNECT / DISCONNECT notifications ---- */
    {
        uint16_t scanCount = count < 32 ? count : 32;
        uint8_t scanBuf[32];
        for (i = 0; i < scanCount; i++) {
            scanBuf[i] = s_ringBuf[(s_ringTail + i) % ESP01S_RINGBUF_SIZE];
        }

        /* Look for ",CONNECT" pattern */
        for (i = 0; i + 8 <= scanCount; i++) {
            if (memcmp(&scanBuf[i], ",CONNECT", 8) == 0) {
                /* Extract link ID: digit before ,CONNECT */
                if (i > 0 && scanBuf[i - 1] >= '0' && scanBuf[i - 1] <= '4') {
                    s_clientLinkId = scanBuf[i - 1] - '0';
                    s_clientConnected = 1;
                    s_connectCount++;
                    printf("[NET] Client CONNECTED link_id=%d\r\n", s_clientLinkId);
                }
                RingBuf_SkipUntil(",CONNECT");
                if (s_ringHead >= s_ringTail)
                    count = s_ringHead - s_ringTail;
                else
                    count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;
                break;
            }
        }

        /* Look for ",CLOSED" pattern */
        scanCount = count < 32 ? count : 32;
        for (i = 0; i < scanCount; i++) {
            scanBuf[i] = s_ringBuf[(s_ringTail + i) % ESP01S_RINGBUF_SIZE];
        }
        for (i = 0; i + 7 <= scanCount; i++) {
            if (memcmp(&scanBuf[i], ",CLOSED", 7) == 0) {
                s_clientConnected = 0;
                s_disconnectCount++;
                printf("[NET] Client DISCONNECTED\r\n");
                RingBuf_SkipUntil(",CLOSED");
                if (s_ringHead >= s_ringTail)
                    count = s_ringHead - s_ringTail;
                else
                    count = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;
                break;
            }
        }
    }

    /* ---- Check for +IPD data ---- */
    /* +IPD,<link_id>,<length>:<data> */
    {
        uint16_t scanCount = count < 128 ? count : 128;
        uint8_t scanBuf[128];
        uint16_t ipdPos = 0xFFFF;
        int foundIPD = 0;

        for (i = 0; i < scanCount; i++) {
            scanBuf[i] = s_ringBuf[(s_ringTail + i) % ESP01S_RINGBUF_SIZE];
        }

        for (i = 0; i + 5 <= scanCount; i++) {
            if (memcmp(&scanBuf[i], "+IPD", 4) == 0) {
                ipdPos = i;
                foundIPD = 1;
                break;
            }
        }

        if (foundIPD) {
            /* Parse: +IPD,<link_id>,<length>:<data> */
            uint16_t pos = ipdPos + 4;  /* After "+IPD" */
            int16_t linkId, dataLen;
            uint16_t endPos;
            uint16_t dataAvail;
            uint16_t dataStartRingIdx;

            /* Expect ',' */
            if (pos >= scanCount || scanBuf[pos] != ',') {
                /* Not a valid +IPD, skip it */
                RingBuf_SkipUntil("+IPD");
                return;
            }
            pos++;  /* Skip ',' */

            /* Parse link_id */
            linkId = RingBuf_ParseNumber((s_ringTail + pos) % ESP01S_RINGBUF_SIZE, &endPos, 2);
            if (linkId < 0) {
                RingBuf_SkipUntil("+IPD");
                return;
            }

            /* Recalculate pos from endPos */
            if (endPos >= s_ringTail)
                pos = endPos - s_ringTail;
            else
                pos = ESP01S_RINGBUF_SIZE - s_ringTail + endPos;

            /* Expect ',' */
            if (pos < scanCount && scanBuf[pos] == ',') {
                pos++;
            }

            /* Parse data length */
            dataLen = RingBuf_ParseNumber((s_ringTail + pos) % ESP01S_RINGBUF_SIZE, &endPos, 5);
            if (dataLen <= 0) {
                RingBuf_SkipUntil("+IPD");
                return;
            }

            /* Recalculate pos from endPos */
            if (endPos >= s_ringTail)
                pos = endPos - s_ringTail;
            else
                pos = ESP01S_RINGBUF_SIZE - s_ringTail + endPos;

            /* Expect ':' */
            if (pos < scanCount && scanBuf[pos] == ':') {
                pos++;
            }

            /* Calculate how many data bytes are actually available in ring buffer */
            {
                uint16_t totalAvail;
                if (s_ringHead >= s_ringTail)
                    totalAvail = s_ringHead - s_ringTail;
                else
                    totalAvail = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;

                dataAvail = totalAvail > pos ? totalAvail - pos : 0;

                if (dataAvail >= (uint16_t)dataLen) {
                    /* We have all the data bytes - extract them */
                    uint8_t extractBuf[128];
                    uint16_t extractLen = (uint16_t)dataLen;
                    if (extractLen > sizeof(extractBuf)) extractLen = sizeof(extractBuf);

                    dataStartRingIdx = (s_ringTail + pos) % ESP01S_RINGBUF_SIZE;
                    for (i = 0; i < extractLen; i++) {
                        extractBuf[i] = s_ringBuf[(dataStartRingIdx + i) % ESP01S_RINGBUF_SIZE];
                    }

                    /* Advance tail past the entire +IPD block */
                    s_ringTail = (dataStartRingIdx + extractLen) % ESP01S_RINGBUF_SIZE;

                    printf("[RX] +IPD len=%d\r\n", extractLen);

                    /* Parse frames from extracted data */
                    ESP01S_ParseFrames(extractBuf, extractLen);
                }
                /* else: not enough data yet, wait for next Process() call */
            }
        }
    }

    /* ---- Garbage collection: if ring buffer is >80% full, discard old data ---- */
    {
        uint16_t used;
        if (s_ringHead >= s_ringTail)
            used = s_ringHead - s_ringTail;
        else
            used = ESP01S_RINGBUF_SIZE - s_ringTail + s_ringHead;

        if (used > (ESP01S_RINGBUF_SIZE * 80 / 100)) {
            /* Discard oldest 25% to prevent overflow */
            uint16_t discard = used / 4;
            s_ringTail = (s_ringTail + discard) % ESP01S_RINGBUF_SIZE;
        }
    }
}

/* ======================== Public Init ======================== */

void ESP01S_Init(void)
{
    /* Initialize USART2 for ESP-01S communication */
    ESP01S_USART_Init();

    /* Flush any startup noise */
    delay_ms(500);
    RingBuf_Flush();

    /* Run AT command configuration sequence */
    ESP01S_ConfigSequence();
}

/* ======================== End of File ======================== */
