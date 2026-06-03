#include "system_controller.h"
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include "led.h"
#include "scd41.h"
#include "i2c_master.h"
#include "wifi_manager.h"
#include "mqtt_app_client.h"
#include "sensor_protocol.h"

static const char *TAG = "sys_ctrl";


/* ================================================================
 *  2. WiFi 事件回调
 * ================================================================ */
static void on_wifi_connect(bool success, void *user_data)
{
    if (success) {
        ESP_LOGI(TAG, "WiFi 已连接");
    } else {
        ESP_LOGE(TAG, "WiFi 连接失败");
    }
}

static void on_wifi_got_ip(esp_ip4_addr_t *ip, void *user_data)
{
    ESP_LOGI(TAG, "获取 IP: " IPSTR, IP2STR(ip));
}

/* ================================================================
 *  3. MQTT 数据接收回调
 * ================================================================ */
static void on_mqtt_data(const char *topic, const char *data, int data_len,
                         void *user_data)
{
    ESP_LOGI(TAG, "MQTT 收到消息, topic: %.*s, data_len: %d",
             (int)strlen(topic), topic, data_len);
}

/* ================================================================
 *  4. 硬件初始化阶段
 * ================================================================ */
static esp_err_t hw_init_phase(void)
{
    // 4.1 LED 初始化
    esp_err_t ret = led_init(48);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LED 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    led_set_brightness_percent(50);
    led_set_color(255,0,0);
    led_start_blink(500);
    
    // 4.3 I2C 总线初始化
    ESP_LOGI(TAG, "初始化 I2C 总线...");
    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4.4 SCD41 传感器初始化
    ESP_LOGI(TAG, "初始化 SCD41 传感器...");
    scd41_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    led_stop_blink();
    return ESP_OK;
}

/* ================================================================
 *  5. SCD41 启动周期性测量（含重试）
 * ================================================================ */
static esp_err_t scd41_start_with_retry(int max_retry)
{
    for (int i = 0; i < max_retry; i++) {
        if (scd41_start_periodic_measurement() == ESP_OK) {
            ESP_LOGI(TAG, "SCD41 周期性测量已启动");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "SCD41 启动失败，第 %d/%d 次重试...", i + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGE(TAG, "SCD41 启动周期性测量失败，请检查传感器连接");
    return ESP_FAIL;
}

/* ================================================================
 *  6. 网络初始化阶段（WiFi + MQTT）
 * ================================================================ */
static esp_err_t network_init_phase(void)
{
    // 6.1 WiFi 初始化并连接
    ESP_LOGI(TAG, "初始化 WiFi...");
    wifi_manager_config_t wifi_cfg = {
        .ssid           = CONFIG_WIFI_MANAGER_SSID,
        .password       = CONFIG_WIFI_MANAGER_PASSWORD,
        .max_retry      = CONFIG_WIFI_MANAGER_MAX_RETRY,
        .auth_threshold = WIFI_AUTH_WPA2_PSK,
        .on_connect     = on_wifi_connect,
        .on_got_ip      = on_wifi_got_ip,
        .user_data      = NULL,
    };
    ESP_ERROR_CHECK(wifi_manager_init(&wifi_cfg));

    esp_err_t ret = wifi_manager_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 连接失败");
        return ret;
    }

    // 6.2 MQTT 初始化并启动
    ESP_LOGI(TAG, "初始化 MQTT 客户端...");
    mqtt_app_client_config_t mqtt_cfg = {
        .broker_uri     = CONFIG_MQTT_BROKER_URI,
        .username       = CONFIG_MQTT_USERNAME,
        .password       = CONFIG_MQTT_PASSWORD,
        .client_id      = CONFIG_MQTT_CLIENT_ID,
        .publish_topic  = CONFIG_MQTT_PUBLISH_TOPIC,
        .subscribe_topic = CONFIG_MQTT_SUBSCRIBE_TOPIC,
        .keepalive      = CONFIG_MQTT_KEEPALIVE,
        .qos            = CONFIG_MQTT_QOS,
        .on_data        = on_mqtt_data,
        .user_data      = NULL,
    };
    ret = mqtt_app_client_init(&mqtt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mqtt_app_client_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MQTT 启动失败: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

/* ================================================================
 *  7. 传感器数据读取并通过 MQTT 上报
 * ================================================================ */
static void sensor_sample_and_publish(void)
{
    if (!scd41_data_ready()) {
        ESP_LOGW(TAG, "SCD41 数据未就绪，跳过本次采样");
        return;
    }

    uint16_t co2;
    float temperature, humidity;
    esp_err_t ret = scd41_read_measurement(&co2, &temperature, &humidity);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "读取 SCD41 数据失败: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "CO2: %u ppm, 温度: %.2f °C, 湿度: %.2f %%",
             co2, temperature, humidity);

    // 通过 MQTT 上报（SDTP 协议，十六进制编码）
    if (!mqtt_app_client_is_connected()) {
        return;
    }

    /* ---- 构建协议帧 ---- */
    sensor_frame_t frame;
    sensor_frame_init(&frame);

    // CO2 (uint16, 2 字节大端)
    uint8_t co2_buf[2];
    co2_buf[0] = (co2 >> 8) & 0xFF;
    co2_buf[1] = co2 & 0xFF;
    sensor_frame_add_tlv(&frame, SENSOR_TYPE_SCD41_CO2, co2_buf, 2);

    // 温度 (float, 4 字节大端 IEEE 754)
    uint8_t temp_buf[4];
    uint32_t temp_raw;
    memcpy(&temp_raw, &temperature, sizeof(temp_raw));
    temp_buf[0] = (temp_raw >> 24) & 0xFF;
    temp_buf[1] = (temp_raw >> 16) & 0xFF;
    temp_buf[2] = (temp_raw >> 8) & 0xFF;
    temp_buf[3] = temp_raw & 0xFF;
    sensor_frame_add_tlv(&frame, SENSOR_TYPE_SCD41_TEMP, temp_buf, 4);

    // 湿度 (float, 4 字节大端 IEEE 754)
    uint8_t hum_buf[4];
    uint32_t hum_raw;
    memcpy(&hum_raw, &humidity, sizeof(hum_raw));
    hum_buf[0] = (hum_raw >> 24) & 0xFF;
    hum_buf[1] = (hum_raw >> 16) & 0xFF;
    hum_buf[2] = (hum_raw >> 8) & 0xFF;
    hum_buf[3] = hum_raw & 0xFF;
    sensor_frame_add_tlv(&frame, SENSOR_TYPE_SCD41_HUM, hum_buf, 4);

    /* ---- 构建二进制帧并十六进制编码 ---- */
    uint8_t bin_buf[SENSOR_PROTO_MAX_FRAME];
    uint8_t bin_len = 0;
    if (!sensor_frame_build(&frame, bin_buf, &bin_len)) {
        ESP_LOGW(TAG, "协议帧构建失败");
        return;
    }

    char hex_buf[SENSOR_PROTO_MAX_HEX];
    if (!sensor_frame_hex_encode(bin_buf, bin_len, hex_buf, sizeof(hex_buf))) {
        ESP_LOGW(TAG, "十六进制编码失败");
        return;
    }

    ESP_LOGI(TAG, "MQTT 上报 (HEX): %s", hex_buf);
    mqtt_app_client_publish(CONFIG_MQTT_PUBLISH_TOPIC, hex_buf,
                            CONFIG_MQTT_QOS, 0);
}

/* ================================================================
 *  8. 主入口 — 串联所有阶段并进入主循环
 * ================================================================ */
void system_controller_run(void)
{
    ESP_LOGI(TAG, "=== 环境监测系统启动 ===");

    /* ---- 阶段 1：硬件初始化 ---- */
    esp_err_t ret = hw_init_phase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "硬件初始化失败，系统终止");
        return;
    }
    ESP_LOGI(TAG, "硬件初始化完成");

    /* ---- 阶段 2：系统就绪指示 ---- */
    led_set_color(0, 255, 0);                   // 绿灯 = 硬件就绪
    ESP_LOGI(TAG, "LED 绿灯亮 - 硬件就绪");
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* ---- 阶段 3：启动传感器周期性测量 ---- */
    scd41_start_with_retry(3);

    /* ---- 阶段 4：网络初始化 ---- */
    ret = network_init_phase();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "网络初始化失败，系统将在离线模式下运行");
    }

    /* ---- 阶段 5：等待系统就绪，进入主循环 ---- */
    led_off();
    ESP_LOGI(TAG, "LED 关闭，等待 MQTT 连接与传感器首次测量就绪...");

    // 等待 MQTT 连接（最多 15 秒）
    int wait_count = 0;
    while (!mqtt_app_client_is_connected() && wait_count < 30) {
        vTaskDelay(pdMS_TO_TICKS(500));
        wait_count++;
    }

    if (mqtt_app_client_is_connected()) {
        ESP_LOGI(TAG, "MQTT 已连接，等待传感器首次测量完成...");
    } else {
        ESP_LOGW(TAG, "MQTT 连接超时，将以离线模式运行");
    }

    // SCD41 周期性测量首次结果约需 5 秒，等待 6 秒确保首次测量完成
    vTaskDelay(pdMS_TO_TICKS(6000));

    ESP_LOGI(TAG, "进入主循环");

    /* ---- 主循环 ---- */
    while (1) {
        // 周期性采集传感器数据并上报
        sensor_sample_and_publish();

        // 心跳日志（每 10 秒）
        ESP_LOGI(TAG, "主循环运行中 | WiFi: %s | MQTT: %s",
                 wifi_manager_is_connected() ? "已连接" : "未连接",
                 mqtt_app_client_is_connected() ? "已连接" : "未连接");

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
