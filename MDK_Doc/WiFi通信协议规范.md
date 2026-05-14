# 智能鱼缸系统 — WiFi 通信协议规范

> 版本：V3.0 | 日期：2026-05-05 | 裸机版
> 关联文件：`MDK_Hardware/bsp_esp01s.c/h`
> 对齐 APP：`com.example.intelligentfish.data.net.Protocol.kt`

---

## 1. 通信架构

```
┌──────────┐     WiFi (TCP)     ┌──────────┐    UART (115200)    ┌──────────┐
│ Android  │ ◄────────────────► │ ESP-01S  │ ◄──────────────────► │  STM32   │
│   APP    │  TCP Socket 直连   │ AP+Server│   AT 指令            │  MCU     │
└──────────┘   Port: 8080       └──────────┘                     └──────────┘
```

| 项目 | 配置 |
|------|------|
| ESP-01S 模式 | **AP 模式**（自建热点） |
| 热点名称 | `FishTank` |
| 热点密码 | `12345678` |
| 默认 IP | `192.168.4.1` |
| TCP 端口 | `8080` |
| 串口 | USART2, PA2(TX)/PA3(RX), 115200bps |

APP 连接方式：手机连入 FishTank 热点 → APP 输入 IP `192.168.4.1`，端口 `8080` → 连接。

---

## 2. 数据帧格式

```
┌───────┬─────┬─────┬────────────────┬───────┬───────┐
│ HEAD  │ CMD │ LEN │    PAYLOAD     │  SUM  │  END  │
│ 0xAA  │ 1B  │ 1B  │   0~255 B      │  1B   │ 0x55  │
└───────┴─────┴─────┴────────────────┴───────┴───────┘
```

| 字段 | 长度 | 说明 |
|------|------|------|
| HEAD | 1B | 帧头 `0xAA` |
| CMD | 1B | 命令字 |
| LEN | 1B | PAYLOAD 长度（0~255） |
| PAYLOAD | N B | 数据载荷 |
| SUM | 1B | 校验和 = `(CMD + LEN + ΣPAYLOAD[i]) & 0xFF` |
| END | 1B | 帧尾 `0x55` |

最小帧 = 5 字节（HEAD+CMD+LEN+SUM+END，无 PAYLOAD）。

---

## 3. 命令字定义

### 3.1 MCU → APP（上报）

| CMD | 名称 | LEN | 说明 |
|-----|------|-----|------|
| `0x01` | 传感器数据 | 14 | 每 1 秒主动推送 |
| `0x02` | 设备状态 | 5 | 状态变化时推送 |
| `0x03` | 报警事件 | 2 | 报警触发/解除时推送 |

### 3.2 APP → MCU（控制）

| CMD | 名称 | LEN | 说明 |
|-----|------|-----|------|
| `0x10` | 查询状态 | 0 | 请求 MCU 上报当前状态 |
| `0x11` | 切换模式 | 1 | 0=自动, 1=手动 |
| `0x12` | 继电器控制 | 2 | relay_id + state |
| `0x13` | 同步阈值 | 20 | 修改全部阈值参数 |
| `0x14` | 触发喂食 | 0 | 立即触发一次喂食 |
| `0x15` | 设置报警 | 1 | 0=关闭, 1=开启 |
| `0x16` | 设置喂食间隔 | 2 | uint16 大端序 (秒) |

---

## 4. PAYLOAD 详细定义

### 4.1 传感器数据 (0x01), LEN=14

```
偏移  长度  字段           类型       说明
0     4    waterTemp      float LE   水温 (°C)
4     4    phValue        float LE   PH 值
8     1    waterLevel     uint8      水位 (0~100%)
9     2    airQuality     uint16 BE  空气质量 (AQI 0~20)
11    1    runMode        uint8      0=自动, 1=手动
12    2    feedCountdown  uint16 BE  喂食倒计时 (秒)
```

> LE = 小端序 (Little-Endian), BE = 大端序 (Big-Endian)

### 4.2 设备状态 (0x02), LEN=5

```
偏移  长度  字段           类型      说明
0     1    relayState     uint8     bit0=加热 bit1=加水 bit2=排水 bit3=增氧
1     1    alarmEnable    uint8     0=关闭, 1=开启
2     1    feeding        uint8     0=空闲, 1=喂食中
3     2    reserved       -         保留 (填0)
```

继电器位定义：

| bit | 继电器 | GPIO |
|-----|--------|------|
| 0 | 加热 | PB12 |
| 1 | 加水 | PB14 |
| 2 | 排水 | PB15 |
| 3 | 增氧 | PA15 |

### 4.3 报警事件 (0x03), LEN=2

```
偏移  长度  字段           类型      说明
0     1    alarmType      uint8     见下表
1     1    alarmStatus    uint8     0=解除, 1=触发
```

报警类型表：

| 值 | 含义 |
|----|------|
| 0x01 | 温度过低 |
| 0x02 | 温度过高 |
| 0x03 | 水位过低 |
| 0x04 | 水位过高 |
| 0x05 | 空气质量超标 |
| 0x06 | PH 过低 |
| 0x07 | PH 过高 |

### 4.4 切换模式 (0x11), LEN=1

```
偏移  长度  字段    类型     说明
0     1    mode    uint8    0=自动, 1=手动
```

### 4.5 继电器控制 (0x12), LEN=2

```
偏移  长度  字段      类型     说明
0     1    relayId   uint8    0=加热 1=加水 2=排水 3=增氧
1     1    state     uint8    0=关, 1=开
```

### 4.6 同步阈值 (0x13), LEN=20

```
偏移  长度  字段            类型       说明
0     4    tempLower       float LE   温度下限
4     4    tempUpper       float LE   温度上限
8     2    airQualityMax   uint16 BE  空气质量阈值
10    4    phLower         float LE   PH 下限
14    4    phUpper         float LE   PH 上限
18    1    waterLevelMin   uint8      水位下限 (%)
19    1    waterLevelMax   uint8      水位上限 (%)
```

### 4.7 设置喂食间隔 (0x16), LEN=2

```
偏移  长度  字段      类型       说明
0     2    seconds   uint16 BE  喂食间隔 (秒, 5~300)
```

---

## 5. ESP-01S AT 指令初始化序列

MCU 上电后依次发送以下 AT 指令配置 ESP-01S：

```
步骤1: 测试通信
  MCU → AT\r\n
  ESP ← OK

步骤2: 设置 AP 模式
  MCU → AT+CWMODE=2\r\n
  ESP ← OK

步骤3: 创建热点
  MCU → AT+CWSAP="FishTank","12345678",1,3\r\n
  ESP ← OK
  (SSID=FishTank, 密码=12345678, 通道1, WPA2_PSK)

步骤4: 开启多连接
  MCU → AT+CIPMUX=1\r\n
  ESP ← OK

步骤5: 开启 TCP 服务器
  MCU → AT+CIPSERVER=1,8080\r\n
  ESP ← OK

步骤6: 设置服务器超时
  MCU → AT+CIPSTO=120\r\n
  ESP ← OK
```

初始化完成后，ESP-01S 创建热点并监听 TCP 8080 端口。

---

## 6. 数据收发机制

### 6.1 发送数据（MCU → APP）

```
MCU 构建帧 → AT+CIPSEND=<link_id>,<len>\r\n → 等待 ">" 提示 → 发送帧数据
```

调用方式：

| 函数 | 说明 | 调用时机 |
|------|------|----------|
| `ESP01S_SendSensorData()` | 发送 CMD 0x01 (14B) | 主循环每 ~1 秒 |
| `ESP01S_SendDeviceStatus()` | 发送 CMD 0x02 (5B) | 继电器状态变化时 |
| `ESP01S_SendAlarm(type, status)` | 发送 CMD 0x03 (2B) | 报警触发/解除时 |

### 6.2 接收数据（APP → MCU）

```
USART2 RX 中断 → 环形缓冲区 (512B)
                         ↓
主循环调用 ESP01S_Process()
                         ↓
解析 "+IPD,<link>,<len>:<data>" 格式
                         ↓
从 data 中提取完整帧 (0xAA...0x55)
                         ↓
校验和验证 → 分发到命令处理函数
```

### 6.3 命令处理

| CMD | 处理函数 | 操作 |
|-----|----------|------|
| 0x10 | 查询 | 立即发送 0x01 + 0x02 |
| 0x11 | 切换模式 | 修改 Record.runMode，切换时清继电器 |
| 0x12 | 继电器控制 | 手动模式下修改对应 Flag.relayXxx |
| 0x13 | 同步阈值 | 修改 Record 中全部阈值字段 |
| 0x14 | 触发喂食 | Flag.feeding=1, feedCountdown=feedInterval |
| 0x15 | 设置报警 | 修改 Flag.alarmEnable |
| 0x16 | 设置喂食间隔 | 修改 Record.feedInterval |

---

## 7. 连接流程图

```
APP                                ESP-01S + STM32
 │                                      │
 │  1. 手机连入 FishTank 热点           │
 │  2. APP TCP Connect(192.168.4.1:8080)│
 │  ──────────────────────────────────► │
 │                                      │  3. ESP-01S 返回 "CONNECT"
 │                                      │  4. MCU 发送 0x01 传感器数据
 │  ◄────────────────────────────────── │
 │  5. APP 显示实时数据                  │
 │                                      │
 │  6. 用户点击"切换手动"                │
 │  7. APP 发送 0x11 {0x01}             │
 │  ──────────────────────────────────► │
 │                                      │  8. MCU 切换 runMode=1
 │                                      │  9. MCU 回复 0x02 设备状态
 │  ◄────────────────────────────────── │
 │  10. APP 更新界面                     │
 │                                      │
 │  11. 用户调节温度阈值                 │
 │  12. APP 发送 0x13 {阈值20B}         │
 │  ──────────────────────────────────► │
 │                                      │  13. MCU 更新 Record 阈值
```

---

## 8. USART2 配置

| 项目 | 值 |
|------|------|
| 引脚 | PA2 (TX, 复用推挽) / PA3 (RX, 浮空输入) |
| 波特率 | 115200 |
| 数据格式 | 8N1 |
| 中断 | RX 中断使能 (优先级 3) |
| 环形缓冲区 | 512 字节, ISR 写入, 主循环读取 |

> 注意：PA3 原为 KEY4 引脚，KEY4 已移至 PB8，PA3 专用于 USART2_RX。

---

## 9. WiFi 状态指示

| OLED Row6 | 含义 |
|-----------|------|
| `W+` | WiFi 已连接（有 APP 客户端） |
| `W-` | WiFi 未连接 |

状态由 `ESP01S_WiFiConnected` 变量控制：
- 收到 ESP-01S 发来 `,CONNECT` → 设为 1
- 收到 `,CLOSED` → 设为 0

---

## 十、变更记录

| 版本 | 日期 | 主要改动内容 |
|------|------|-------------|
| V3.0 | 2026-05-05 | 全面重写：AP 模式替代 Station 模式；帧格式与 APP Protocol.kt 完全对齐；新增 7 个 APP→MCU 命令处理；新增 3 个 MCU→APP 帧发送函数；USART2 使用标准外设库替代寄存器操作；新增环形缓冲区中断接收机制；新增 AT 指令初始化序列；新增连接流程图 |
| V2.0 | 2026-05-02 | 初版协议草案 |

---

*协议结束。*
