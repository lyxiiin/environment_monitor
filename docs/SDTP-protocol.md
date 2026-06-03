# SDTP — 传感器数据传输协议 (Sensor Data Transmission Protocol)

## 概述

SDTP 是本环境监测系统自定义的传感器数据编码协议，用于将多传感器数据打包为一条紧凑的二进制帧，经十六进制编码后通过 MQTT 文本主题发布。

**设计目标**：

- **紧凑**：二进制 TLV 编码，最小化传输字节
- **可靠**：帧头同步字 + CRC8 校验，接收端可校验完整性
- **可扩展**：传感器类型通过枚举 ID 标识，新增传感器无需改动帧结构

## 帧结构

```
┌──────────┬──────────┬───────────┬───────────┬──────────────────┬──────┐
│  SYNC_HI │ SYNC_LO  │ Timestamp │ TLV Count │   TLV Records    │ CRC8 │
│  0xAA    │  0x55    │  4 bytes  │  1 byte   │    variable      │1 byte│
└──────────┴──────────┴───────────┴───────────┴──────────────────┴──────┘
```

### 字段说明

| 偏移 | 长度 | 字段 | 说明 |
| ---- | ---- | ---- | ---- |
| 0 | 1 | SYNC_HI | 同步头高字节，固定 `0xAA` |
| 1 | 1 | SYNC_LO | 同步头低字节，固定 `0x55` |
| 2 | 4 | Timestamp | Unix 时间戳，大端序 uint32 |
| 6 | 1 | TLV Count | 本帧包含的 TLV 记录数量（1~16） |
| 7 | N | TLV Records | 连续排列的 TLV 记录 |
| 7+N | 1 | CRC8 | 校验码（覆盖 Timestamp + Count + TLV Records，不含同步头） |

### TLV 记录格式

每条 TLV 记录由三部分组成：

```
┌──────┬────────┬─────────────────┐
│ Type │ Length │      Value      │
│ 1 B  │  1 B   │   Length bytes  │
└──────┴────────┴─────────────────┘
```

| 字段 | 长度 | 说明 |
| ---- | ---- | ---- |
| Type | 1 byte | 传感器类型枚举 ID，见下方传感器类型表 |
| Length | 1 byte | Value 字段的字节数（1~8） |
| Value | 1~8 bytes | 传感器数据值，大端序存储 |

## CRC8 校验

- **算法**：CRC-8，多项式 `0x07`（x⁸ + x² + x¹ + 1）
- **初始值**：`0x00`
- **校验范围**：从 Timestamp 到最后一字节 TLV 数据（即 SYNC 之后、CRC 之前的所有字节）
- **计算代码**（C 语言）：

```c
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}
```

## 十六进制编码

二进制帧构建完成后，逐字节转为两个大写十六进制字符。例如 `[0xAA, 0x55]` → `"AA55"`。

编码后通过 MQTT `publish` 接口发送，topic 为 `env_monitor/data`（可配置）。

## 传感器类型枚举

| Type ID | 传感器 | 数据含义 | 数据类型 | Value 字节数 |
| ------- | ------ | -------- | -------- | ------------ |
| `0x01` | SCD41 | CO₂ 浓度 | uint16 (大端) | 2 |
| `0x02` | SCD41 | 温度 | float (IEEE 754 大端) | 4 |
| `0x03` | SCD41 | 湿度 | float (IEEE 754 大端) | 4 |
| `0x04` | SHT40 | 温度 | float (IEEE 754 大端) | 4 |
| `0x05` | SHT40 | 湿度 | float (IEEE 754 大端) | 4 |
| `0x06~0xEF` | — | **预留给未来传感器** | — | — |
| `0xF0~0xFF` | — | **扩展区** | — | — |

> 定义来源：[sensor_protocol.h](../components/sensor_protocol/sensor_protocol.h)

## 示例：解析一帧数据

假设接收到十六进制字符串：

```
AA55000000010301000204012345670340400000...CRC
```

按帧结构拆解：

| 字节序列 | 含义 | 值 |
| -------- | ---- | -- |
| `AA` | SYNC_HI | `0xAA` ✓ |
| `55` | SYNC_LO | `0x55` ✓ |
| `00000001` | Timestamp | Unix 时间戳 = 1 |
| `03` | TLV Count | 本帧含 3 条记录 |

**TLV 记录 1**：

| 字节 | 含义 |
| ---- | ---- |
| `01` | Type = SCD41 CO₂ |
| `02` | Length = 2 bytes |
| `0002` | Value = 2 → CO₂ = 2 ppm |

**TLV 记录 2**：

| 字节 | 含义 |
| ---- | ---- |
| `04` | Type = SHT40 温度 |
| `04` | Length = 4 bytes |
| `01234567` | Value (float 大端) → 温度 ≈ 2.39×10⁻³⁸ °C（示例值） |

**TLV 记录 3**：

| 字节 | 含义 |
| ---- | ---- |
| `03` | Type = SCD41 湿度 |
| `04` | Length = 4 bytes |
| `40400000` | Value (float 大端 IEEE 754) → 湿度 = 3.0 %RH |

> 注：示例中值为演示用，实际数据由传感器实时采集。

## 添加新传感器类型

1. 在 `sensor_type_t` 枚举中新增类型 ID（如 `SENSOR_TYPE_BME280_TEMP = 0x06`）
2. 在系统主循环中调用 `sensor_frame_add_tlv()` 追加该传感器的 TLV 记录
3. 在本文档的「传感器类型枚举」表格中添加对应行

帧结构、CRC 算法、十六进制编码均无需修改。