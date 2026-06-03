# 环境监测系统 (Environment Monitor)

基于 **ESP32** + **ESP-IDF v6.0** 的物联网环境监测设备，通过 SCD41 传感器采集环境数据（CO₂、温度、湿度），使用自定义 SDTP 协议通过 MQTT 上报至云端。

## 功能特性

- **多传感器采集**：集成 SCD41（CO₂ / 温度 / 湿度）和 SHT40（温度 / 湿度，预留）传感器
- **LED 状态指示**：使用 WS2812 可编程 RGB LED，通过颜色和闪烁直观反馈系统运行状态
- **WiFi 自动连接**：支持自动重连，最多重试次数可配置
- **MQTT 数据上报**：通过 MQTT 协议将传感器数据发布到云端 Broker
- **SDTP 自定义协议**：二进制 TLV 帧 + CRC8 校验 + 十六进制编码，高效可靠的数据传输
- **离线容错**：网络不可用时系统继续本地运行，不阻塞主循环

## 硬件要求

| 硬件          | 说明                      |
| ------------- | ------------------------- |
| 主控          | ESP32-S3 (Xtensa LX7)       |
| CO₂ 传感器    | SCD41 (I²C, 地址 0x62)    |
| 温湿度传感器  | SHT40 (I²C, 地址 0x44，预留) |
| LED           | WS2812 RGB LED            |
| 接口          | I²C 总线 (SDA/SCL)         |

### 引脚接线

| 功能         | GPIO  | 说明                     |
| ------------ | ----- | ------------------------ |
| I²C SDA      | GPIO8 | 连接 SCD41 / SHT40 SDA   |
| I²C SCL      | GPIO9 | 连接 SCD41 / SHT40 SCL   |
| WS2812 LED   | GPIO48 | RGB LED 数据输入         |

## 项目架构

```
environment_monitor/
├── main/
│   ├── main.c                 # 启动入口（极简，仅调用 system_controller_run）
│   └── CMakeLists.txt
├── components/
│   ├── system_controller/     # 系统控制器：串联硬件初始化、网络连接、主循环
│   ├── SCD41/                 # SCD41 CO₂/温度/湿度传感器驱动
│   ├── SHT40/                 # SHT40 温湿度传感器驱动（预留）
│   ├── i2c_driver/            # I²C 总线主机驱动
│   ├── led/                   # WS2812 RGB LED 控制（颜色、亮度、闪烁）
│   ├── wifi_manager/          # WiFi 连接管理（自动重连、状态回调）
│   ├── mqtt_app_client/       # MQTT 客户端封装（发布/订阅/状态查询）
│   └── sensor_protocol/       # SDTP 传感器数据传输协议
├── managed_components/        # ESP-IDF 托管组件（i2cdev, scd4x, sht4x, mqtt 等）
├── CMakeLists.txt             # 顶层 CMake 构建配置
├── sdkconfig.defaults         # ESP-IDF 默认配置
└── dependencies.lock          # 托管组件版本锁定
```

## 组件说明

### 核心组件

#### system_controller — 系统控制器

系统初始化和主循环的编排者，执行以下阶段：

1. **硬件初始化**：LED → I²C → SCD41
2. **传感器启动**：SCD41 启动周期性测量（含重试）
3. **网络初始化**：WiFi 连接 → MQTT 连接
4. **主循环**：每 10 秒采集一次数据，通过 MQTT 上报

LED 状态变化：红灯闪烁（初始化中）→ 绿灯常亮（硬件就绪）→ 关闭进入主循环。

### 传感器驱动

| 传感器 | I²C 地址 | 测量参数 | 状态 |
| ------ | -------- | -------- | ---- |
| SCD41 | 0x62 | CO₂ / 温度 / 湿度 | 已集成 |
| SHT40 | 0x44 | 温度 / 湿度 | 预留 |

#### SCD41 — CO₂ 传感器驱动

基于 I²C 总线驱动 Sensirion SCD41 传感器，提供：

- `scd41_init`：传感器初始化（单次测量触发以清除残留状态）
- `scd41_start_periodic_measurement`：启动周期性测量模式
- `scd41_data_ready`：查询新数据是否就绪
- `scd41_read_measurement`：读取 CO₂(ppm)、温度(°C)、湿度(%RH)

#### SHT40 — 温湿度传感器驱动（预留）

预留的 SHT40 温湿度传感器驱动，当前未在主循环中使用，可按需扩展。

### 通信组件

| 组件 | 功能 |
| ---- | ---- |
| wifi_manager | WiFi 连接管理（自动重连、状态回调） |
| mqtt_app_client | MQTT 客户端封装（发布/订阅/状态查询） |
| sensor_protocol | SDTP 传感器数据编码协议 |

#### wifi_manager — WiFi 管理

WiFi 连接生命周期管理，支持：

- 连接状态回调（连接成功/失败、获取 IP）
- 自动重连（可配置最大重试次数）
- 实时状态查询 `wifi_manager_is_connected`

#### mqtt_app_client — MQTT 客户端

基于 ESP-IDF 托管 MQTT 组件的应用层封装，提供：

- 发布/订阅接口
- 数据接收回调
- 连接状态查询 `mqtt_app_client_is_connected`

#### sensor_protocol — SDTP 协议

传感器数据通过自定义 **SDTP 协议**（二进制 TLV 帧 + CRC8 + 十六进制编码）打包后由 MQTT 上报。协议详细规范见 [docs/SDTP-protocol.md](docs/SDTP-protocol.md)。

### 基础组件

| 组件 | 功能 |
| ---- | ---- |
| i2c_driver | I²C 总线主机驱动，供所有传感器共用 |
| led | WS2812 RGB LED 控制（颜色、亮度、闪烁） |

#### i2c_driver — I²C 总线驱动

I²C 主机初始化，提供统一的 `i2c_master_init` 接口，供所有 I²C 传感器共用同一总线。

#### led — RGB LED 控制

驱动 WS2812 可编程 RGB LED（基于 RMT 外设），提供：

- `led_init`：初始化（指定 GPIO）
- `led_set_color`：设置 RGB 颜色（0-255）
- `led_set_brightness_percent`：设置亮度百分比
- `led_start_blink` / `led_stop_blink`：启动/停止闪烁

## 配置选项

通过 `idf.py menuconfig` 配置以下参数：

### WiFi 配置 (WiFi Manager Configuration)

| 配置项                       | 默认值       | 说明         |
| ---------------------------- | ------------ | ------------ |
| `WIFI_MANAGER_SSID`          | YOUR_WIFI_SSID   | WiFi 名称    |
| `WIFI_MANAGER_PASSWORD`      | YOUR_WIFI_PASSWORD | WiFi 密码  |
| `WIFI_MANAGER_MAX_RETRY`     | 5            | 最大重试次数 |

### MQTT 配置 (MQTT Client Configuration)

| 配置项                  | 默认值                       | 说明         |
| ----------------------- | ---------------------------- | ------------ |
| `MQTT_BROKER_URI`       | mqtt://YOUR_BROKER_IP:1883   | Broker 地址  |
| `MQTT_CLIENT_ID`        | esp32_env_monitor_001        | 客户端 ID    |
| `MQTT_USERNAME`         | YOUR_MQTT_USERNAME           | 用户名       |
| `MQTT_PASSWORD`         | YOUR_MQTT_PASSWORD           | 密码         |
| `MQTT_PUBLISH_TOPIC`    | env_monitor/data             | 发布主题     |
| `MQTT_SUBSCRIBE_TOPIC`  | env_monitor/command          | 订阅主题     |
| `MQTT_KEEPALIVE`        | 60                           | 心跳间隔(秒) |
| `MQTT_QOS`              | 1                            | QoS 等级     |

## 快速开始

### 前置条件

- ESP-IDF v6.0 或更高版本
- Python 3.x（ESP-IDF 依赖）
- ESP32-S3 开发板

### 构建与烧录

```bash
# 1. 激活 ESP-IDF 环境
. $IDF_PATH/export.sh

# 2. 配置项目（设置 WiFi、MQTT 等参数）
idf.py menuconfig
# → WiFi Manager Configuration: 设置 SSID 和密码
# → MQTT Client Configuration: 设置 Broker 地址、用户名和密码

# 3. 编译
idf.py build

# 4. 烧录到 ESP32
idf.py -p /dev/ttyUSB0 flash

# 5. 查看串口日志
idf.py -p /dev/ttyUSB0 monitor
```

### 预期运行日志

启动后串口输出大致如下：

```
I (1234) sys_ctrl: === 环境监测系统启动 ===
I (1234) sys_ctrl: 初始化 I2C 总线...
I (2234) sys_ctrl: 初始化 SCD41 传感器...
I (3234) sys_ctrl: 硬件初始化完成
I (5234) sys_ctrl: LED 绿灯亮 - 硬件就绪
I (6234) sys_ctrl: SCD41 周期性测量已启动
I (6234) sys_ctrl: 初始化 WiFi...
I (8234) sys_ctrl: WiFi 已连接
I (8234) sys_ctrl: 获取 IP: 192.168.1.100
I (8234) sys_ctrl: 初始化 MQTT 客户端...
I (10234) sys_ctrl: CO2: 450 ppm, 温度: 25.30 °C, 湿度: 55.20 %
I (10234) sys_ctrl: MQTT 上报 (HEX): AA550000000100010002...
```

## 技术栈

- **MCU 平台**：ESP32 (Xtensa / RISC-V)
- **RTOS**：FreeRTOS (ESP-IDF 内置)
- **构建系统**：CMake + ESP-IDF Build System
- **协议栈**：lwIP (TCP/IP) + MQTT (esp_mqtt)
- **传感器接口**：I²C (Sensirion SCD41 / SHT40)
- **LED 驱动**：RMT (WS2812)
- **数据协议**：SDTP (Sensor Data Transmission Protocol)，TLV 帧 + CRC8

## 依赖项

通过 ESP-IDF 组件管理器 (`idf_component.yml`) 管理的托管组件：

- `espressif/mqtt` — MQTT 客户端库
- `espressif/led_strip` — WS2812 LED 驱动
- `esp-idf-lib/scd4x` — SCD4x 传感器驱动
- `esp-idf-lib/sht4x` — SHT4x 传感器驱动
- `esp-idf-lib/i2cdev` — I²C 设备抽象层
- `esp-idf-lib/esp_idf_lib_helpers` — 辅助工具

本地组件（`components/` 目录下开发的定制组件）：

- `SCD41`、`SHT40`、`i2c_driver`、`led`、`wifi_manager`、`mqtt_app_client`、`sensor_protocol`、`system_controller`

## 许可证

本项目基于 ESP-IDF 构建，继承 Apache-2.0 许可证。各托管组件遵循其各自的许可证。