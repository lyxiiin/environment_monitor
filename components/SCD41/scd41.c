#include "scd41.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c.h>

static const char *TAG = "SCD41";

// SCD41 命令宏
#define SCD41_CMD_STOP_PERIODIC_MEASUREMENT   0x3F86
#define SCD41_CMD_START_PERIODIC_MEASUREMENT  0x21B1
#define SCD41_CMD_READ_MEASUREMENT            0xEC05
#define SCD41_CMD_GET_DATA_READY              0xE4B8

#define I2C_MASTER_NUM  I2C_NUM_0
#define SCD41_SENSIR_ADDR 0x62


/* ========== 2. CRC-8 校验（Sensirion 全家桶通用） ========== */
static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void scd41_init(void){
    esp_err_t ret = scd41_send_command(SCD41_CMD_STOP_PERIODIC_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "停止周期性测量命令发送失败(可能传感器已在空闲状态): %s", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "传感器重置为空闲状态");
}

esp_err_t scd41_send_command(uint16_t command){
    uint8_t buf[2] = {(command >> 8) & 0xFF, command & 0xFF};
    return i2c_master_write_to_device(
        I2C_MASTER_NUM,
        SCD41_SENSIR_ADDR,
        buf, sizeof(buf),
        pdMS_TO_TICKS(100)
    );
}

esp_err_t scd41_start_periodic_measurement(void){
    return scd41_send_command(SCD41_CMD_START_PERIODIC_MEASUREMENT);
}

bool scd41_data_ready(void){
    esp_err_t ret = scd41_send_command(SCD41_CMD_GET_DATA_READY);
    if(ret != ESP_OK){
        ESP_LOGW(TAG, "发送获取数据就绪命令失败: %s", esp_err_to_name(ret));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t raw[3] = {0};
    ret = i2c_master_read_from_device(
        I2C_MASTER_NUM,
        SCD41_SENSIR_ADDR,
        raw,sizeof(raw),
        pdMS_TO_TICKS(100)
    );

    if(ret != ESP_OK){
        ESP_LOGW(TAG, "读取data_ready状态失败: %s", esp_err_to_name(ret));
        return false;
    }
    if(crc8(raw,2) != raw[2]){
        ESP_LOGW(TAG, "CRC校验失败");
        return false;
    }
    uint16_t status = (raw[0] << 8) | raw[1];
    bool ready = (status & 0x07FF) != 0;
    if (!ready) {
        ESP_LOGW(TAG, "数据未就绪, status=0x%04X (raw: %02X %02X %02X)", status, raw[0], raw[1], raw[2]);
    }
    return ready;
}

esp_err_t scd41_read_measurement(uint16_t *co2_ppm, float *temperature, float *humidity){
    if(!co2_ppm || !temperature || !humidity){
        return ESP_ERR_INVALID_ARG;
    } 
    esp_err_t ret = scd41_send_command(SCD41_CMD_READ_MEASUREMENT);
    if(ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(1));

    uint8_t raw[9] = {0};
    ret = i2c_master_read_from_device(
        I2C_MASTER_NUM,
        SCD41_SENSIR_ADDR,
        raw, sizeof(raw),
        pdMS_TO_TICKS(100)
    );
    if(ret != ESP_OK) return ret;

    if (crc8(raw + 0, 2) != raw[2]) { ESP_LOGE(TAG, "CO2数据CRC校验失败"); return ESP_FAIL; }
    if (crc8(raw + 3, 2) != raw[5]) { ESP_LOGE(TAG, "温度数据CRC校验失败"); return ESP_FAIL; }
    if (crc8(raw + 6, 2) != raw[8]) { ESP_LOGE(TAG, "湿度数据CRC校验失败");   return ESP_FAIL; }

    *co2_ppm        = (raw[0] << 8) | raw[1];
    *temperature = -45.0f + 175.0f * ((float)((raw[3] << 8) | raw[4])) / 65536.0f;
    *humidity   = 100.0f * ((float)((raw[6] << 8) | raw[7])) / 65536.0f;
    return ESP_OK;
}