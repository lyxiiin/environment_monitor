#ifndef __LED_H
#define __LED_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED 模块初始化
 *
 * @param gpio_num LED 引脚号
 * @return esp_err_t 错误码
 */
esp_err_t led_init(uint8_t gpio_num);

/**
 * @brief LED 模块释放
 *
 * @return esp_err_t 错误码
 */
esp_err_t led_deinit();

/**
 * @brief 设置 LED 颜色
 * @param r 红色 0-255
 * @param g 绿色 0-255
 * @param b 蓝色 0-255
 * @return esp_err_t 错误码
 */
esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 设置 LED 亮度 (0-100)
 * @param percent 亮度百分比
 * @return esp_err_t 错误码
 */
esp_err_t led_set_brightness_percent(uint8_t percent);

/**
 * @brief 设置 LED 颜色（原始，跳过亮度计算，适用于高速闪烁）
 * @param r 红色 0-255
 * @param g 绿色 0-255
 * @param b 蓝色 0-255
 * @return esp_err_t 错误码
 */
esp_err_t led_set_color_raw(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief 清除 LED（原始，跳过亮度计算，适用于高速闪烁）
 * @return esp_err_t 错误码
 */
esp_err_t led_clear_raw(void);

/**
 * @brief 关闭 LED
 * @return esp_err_t 错误码
 */
esp_err_t led_off();

/**
 * @brief 开启 LED 闪烁
 * @param interval_ms 闪烁间隔（毫秒）
 * @return esp_err_t 错误码
 */
esp_err_t led_start_blink(uint32_t interval_ms);

/**
 * @brief 停止 LED 闪烁
 * @return esp_err_t 错误码
 */
esp_err_t led_stop_blink();

#ifdef __cplusplus
}

#endif

#endif
