#ifndef __SCD41_H
#define __SCD41_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief SCD41传感器初始化
 *
*/
void scd41_init(void);

/*
 * @brief 向SCD41传感器发送命令
 * @param uint16_t command 命令
 * @return esp_err_t 错误码
 *
*/
esp_err_t scd41_send_command(uint16_t command);

/*
 * @brief 启动周期性测量
 *
*/
esp_err_t scd41_start_periodic_measurement(void);

/*
 * @brief 查询数据是否准备就绪
 * @param 
 * @return bool 
 *
*/

bool scd41_data_ready(void);

/*
 * @brief 从SCD41传感器读取数据
 * @param uint16_t *co2_ppm CO2浓度存储位置
 * @param float *temperature 温度存储位置
 * @param float *humidity 湿度存储位置
 * @return esp_err_t 错误码
 *
*/
esp_err_t scd41_read_measurement(uint16_t *co2_ppm, float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif

#endif