#include "led.h"
#include <esp_log.h>
#include <led_strip.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

static const char *TAG = "led";

// LED Strip 句柄
static led_strip_handle_t led_strip = NULL;
static uint8_t led_gpio = GPIO_NUM_48;

// 闪烁任务
static TaskHandle_t blink_task_handle = NULL;
static volatile bool blink_running = false;
static uint32_t blink_interval_ms = 1000;
static SemaphoreHandle_t blink_stopped_sem = NULL;  // 信号量：闪烁任务已退出

// 颜色缓存
static uint8_t current_r = 0;
static uint8_t current_g = 0;
static uint8_t current_b = 0;
static uint8_t brightness_percent = 50;

/**
 * @brief 根据当前全局亮度百分比衰减颜色分量值
 *
 * 将 0~255 范围的原始颜色分量按 brightness_percent 百分比进行线性缩放，
 * 用于在调用 led_strip_set_pixel 前统一计算最终输出值。
 *
 * @param[in] color_value 原始颜色分量值 (0~255)
 * @return uint8_t 衰减后的颜色分量值 (0~255)
 * @note 此函数不检查 led_strip 是否已初始化，调用者需确保上下文有效。
 */
static uint8_t apply_brightness(uint8_t color_value)
{
    return (color_value * brightness_percent) / 100;
}

/**
 * @brief 将 LED 灯条缓冲区刷新到硬件
 *
 * 调用 espressif__led_strip 库的 led_strip_refresh() 将当前设置的像素数据
 * 通过 RMT 外设发送到 WS2812 LED。若刷新失败，通过 ESP_LOGE 输出错误码。
 *
 * @return esp_err_t
 *         - ESP_OK: 刷新成功
 *         - 其他: 底层 RMT 传输失败的错误码
 */
static esp_err_t led_flush(void)
{
    esp_err_t ret = led_strip_refresh(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "刷新LED灯条失败: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief FreeRTOS 闪烁任务入口函数
 *
 * 循环执行 LED 亮/灭交替，每半个 interval 切换一次状态：
 *   - 亮状态：以当前缓存的 (current_r, current_g, current_b) 颜色和亮度百分比设置像素并刷新
 *   - 灭状态：调用 led_strip_clear() 清除像素并刷新
 *
 * 任务在 blink_running == false 时退出，退出前通过 blink_stopped_sem 信号量
 * 通知调用者，并自删除（vTaskDelete(NULL)）。
 *
 * @param[in] pvParameter 未使用的任务参数（FreeRTOS 任务签名要求）
 * @note 任务栈大小由 led_start_blink() 中 xTaskCreate 的第三个参数决定（2048 字）。
 */
static void blink_task(void *pvParameter)
{
    while (blink_running) {
        // 亮：设置颜色并刷新
        led_strip_set_pixel(led_strip, 0,
                            apply_brightness(current_r),
                            apply_brightness(current_g),
                            apply_brightness(current_b));
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(blink_interval_ms / 2));

        if (!blink_running) break;

        // 灭：清除并刷新
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(blink_interval_ms / 2));
    }

    // 通知调用者：闪烁任务已退出
    blink_task_handle = NULL;
    if (blink_stopped_sem != NULL) {
        xSemaphoreGive(blink_stopped_sem);
    }
    vTaskDelete(NULL);
}

/**
 * @brief 初始化 LED 模块
 *
 * 执行以下步骤：
 *   1. 记录 GPIO 引脚号到全局变量 led_gpio
 *   2. 创建 blink_stopped_sem 二进制信号量（若未创建）
 *   3. 配置 led_strip_config_t（WS2812 型号，GRB 色彩格式，单像素）
 *   4. 配置 led_strip_rmt_config_t（RMT 时钟源 10MHz，64 个内存块符号）
 *   5. 调用 led_strip_new_rmt_device() 创建 RMT 通道和 LED 灯条句柄
 *   6. 清除 LED 像素并刷新，确保 LED 初始为关闭状态
 *
 * @param[in] gpio_num 连接 WS2812 数据线的 GPIO 编号（如 GPIO_NUM_48）
 * @return esp_err_t
 *         - ESP_OK: 初始化成功
 *         - ESP_ERR_NO_MEM: 信号量创建失败
 *         - 其他: led_strip_new_rmt_device() 或 led_strip_refresh() 返回的错误码
 */
esp_err_t led_init(uint8_t gpio_num)
{
    ESP_LOGI(TAG, "正在初始化GPIO %d上的LED", gpio_num);
    led_gpio = gpio_num;

    // 创建闪烁停止信号量（如果尚未创建）
    if (blink_stopped_sem == NULL) {
        blink_stopped_sem = xSemaphoreCreateBinary();
        if (blink_stopped_sem == NULL) {
            ESP_LOGE(TAG, "创建闪烁信号量失败");
            return ESP_ERR_NO_MEM;
        }
    }

    // LED Strip 配置
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags = {
            .invert_out = false,
        },
    };

    // RMT 配置
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,  // 10MHz
        .mem_block_symbols = 64,
        .flags = {
            .with_dma = false,
        },
    };

    // 创建 LED Strip 句柄
    esp_err_t ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建LED灯条设备失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化 LED 为关闭状态
    led_strip_clear(led_strip);
    ret = led_strip_refresh(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "初始化后刷新LED失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LED初始化成功");
    return ESP_OK;
}

/**
 * @brief 反初始化 LED 模块，释放所有资源
 *
 * 执行以下步骤：
 *   1. 若 led_strip 句柄为空，直接返回 ESP_OK（幂等操作）
 *   2. 调用 led_stop_blink() 停止并等待闪烁任务退出
 *   3. 清除 LED 像素并刷新
 *   4. 调用 led_strip_del() 释放 RMT 通道及 LED 灯条资源
 *   5. 删除 blink_stopped_sem 信号量
 *
 * @return esp_err_t 始终返回 ESP_OK（错误仅通过 ESP_LOGW 记录，不影响返回值）
 * @note 调用后 led_strip 句柄置为 NULL，可再次调用 led_init() 重新初始化。
 */
esp_err_t led_deinit(void)
{
    ESP_LOGI(TAG, "正在反初始化LED");

    if (led_strip == NULL) {
        return ESP_OK;
    }

    // 停止闪烁任务（等待其真正退出）
    led_stop_blink();

    // 关闭 LED
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);

    // 释放 RMT 资源
    esp_err_t ret = led_strip_del(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "删除LED灯条失败: %s", esp_err_to_name(ret));
    }
    led_strip = NULL;

    // 释放信号量
    if (blink_stopped_sem != NULL) {
        vSemaphoreDelete(blink_stopped_sem);
        blink_stopped_sem = NULL;
    }

    return ESP_OK;
}

/**
 * @brief 设置 LED 颜色并刷新显示
 *
 * 将 RGB 颜色值缓存到全局变量 (current_r, current_g, current_b)，
 * 经 apply_brightness() 亮度衰减后写入 LED 像素，最后调用 led_flush() 刷新硬件。
 *
 * @param[in] r 红色分量 (0~255)
 * @param[in] g 绿色分量 (0~255)
 * @param[in] b 蓝色分量 (0~255)
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_STATE: LED 未初始化
 *         - 其他: 底层 set_pixel 或 refresh 失败的错误码
 * @note 颜色值会被存储，后续调用 led_set_brightness_percent() 时自动重新应用。
 */
esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    // 存储原始颜色值
    current_r = r;
    current_g = g;
    current_b = b;

    // 设置像素并刷新
    esp_err_t ret = led_strip_set_pixel(led_strip, 0,
                                        apply_brightness(r),
                                        apply_brightness(g),
                                        apply_brightness(b));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置像素失败: %s", esp_err_to_name(ret));
        return ret;
    }

    return led_flush();
}

/**
 * @brief 设置 LED 亮度百分比
 *
 * 更新全局亮度百分比变量 brightness_percent，并立即以新亮度重新应用
 * 当前缓存的 (current_r, current_g, current_b) 颜色值到 LED 硬件。
 *
 * @param[in] percent 亮度百分比 (0~100)，超过 100 自动截断为 100
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_STATE: LED 未初始化
 *         - 其他: 底层 set_pixel 或 refresh 失败的错误码
 * @note 若 LED 当前关闭（颜色为 0,0,0），调用此函数后 LED 保持关闭状态。
 */
esp_err_t led_set_brightness_percent(uint8_t percent)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    if (percent > 100) {
        percent = 100;
    }

    ESP_LOGI(TAG, "设置亮度为%d%%", percent);
    brightness_percent = percent;

    // 重新应用亮度并刷新
    esp_err_t ret = led_strip_set_pixel(led_strip, 0,
                                        apply_brightness(current_r),
                                        apply_brightness(current_g),
                                        apply_brightness(current_b));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置像素失败: %s", esp_err_to_name(ret));
        return ret;
    }

    return led_flush();
}

/**
 * @brief 设置 LED 颜色（原始模式，不经过亮度计算）
 *
 * 直接将 RGB 值写入 LED 像素并刷新，跳过 apply_brightness() 亮度衰减。
 * 适用于需要精确控制颜色值的高速场景（如闪烁任务中的亮状态）。
 *
 * @param[in] r 红色分量 (0~255)
 * @param[in] g 绿色分量 (0~255)
 * @param[in] b 蓝色分量 (0~255)
 * @return esp_err_t
 *         - ESP_OK: 设置成功
 *         - ESP_ERR_INVALID_STATE: LED 未初始化
 *         - 其他: 底层 set_pixel 或 refresh 失败的错误码
 * @note 此函数不会更新 (current_r, current_g, current_b) 缓存，因此后续
 *       调用 led_set_brightness_percent() 时不会影响此函数设置的颜色。
 */
esp_err_t led_set_color_raw(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = led_strip_set_pixel(led_strip, 0, r, g, b);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "设置像素失败: %s", esp_err_to_name(ret));
        return ret;
    }

    return led_flush();
}

/**
 * @brief 清除 LED 像素（原始模式，不经过亮度计算）
 *
 * 调用 led_strip_clear() 将所有像素值清零并刷新硬件。
 * 适用于需要快速熄灭 LED 的高速场景（如闪烁任务中的灭状态）。
 *
 * @return esp_err_t
 *         - ESP_OK: 清除成功
 *         - ESP_ERR_INVALID_STATE: LED 未初始化
 *         - 其他: 底层 clear 或 refresh 失败的错误码
 * @note 与 led_off() 功能等价，独立命名以和 led_set_color_raw() 语义保持一致。
 */
esp_err_t led_clear_raw(void)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = led_strip_clear(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED: %s", esp_err_to_name(ret));
        return ret;
    }

    return led_flush();
}

/**
 * @brief 关闭 LED（清除所有像素并刷新）
 *
 * 调用 led_strip_clear() + led_flush() 将 LED 熄灭。
 * 功能与 led_clear_raw() 完全相同。
 *
 * @return esp_err_t
 *         - ESP_OK: 关闭成功
 *         - ESP_ERR_INVALID_STATE: LED 未初始化
 *         - 其他: 底层 clear 或 refresh 失败的错误码
 */
esp_err_t led_off(void)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = led_strip_clear(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED: %s", esp_err_to_name(ret));
        return ret;
    }

    return led_flush();
}

/**
 * @brief 启动 LED 闪烁
 *
 * 若已有闪烁任务在运行，先调用 led_stop_blink() 等待其退出。
 * 然后创建 FreeRTOS 任务 blink_task，以指定间隔循环亮/灭 LED。
 *
 * @param[in] interval_ms 闪烁周期（毫秒），半个周期亮、半个周期灭
 * @return esp_err_t
 *         - ESP_OK: 闪烁任务创建成功
 *         - ESP_ERR_INVALID_STATE: LED 未初始化
 *         - ESP_ERR_NO_MEM: xTaskCreate() 内存不足
 * @note 任务栈大小固定为 2048 字，优先级为 5。
 */
esp_err_t led_start_blink(uint32_t interval_ms)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED未初始化");
        return ESP_ERR_INVALID_STATE;
    }

    // 如果已有闪烁在运行，先停止
    if (blink_task_handle != NULL) {
        led_stop_blink();
    }

    blink_running = true;
    blink_interval_ms = interval_ms;

    BaseType_t ret = xTaskCreate(
        blink_task,
        "led_blink",
        2048,
        NULL,
        5,  // 优先级
        &blink_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建闪烁任务失败");
        blink_running = false;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "闪烁已启动，间隔%lu ms", interval_ms);
    return ESP_OK;
}

/**
 * @brief 停止 LED 闪烁
 *
 * 设置 blink_running = false 通知闪烁任务退出，然后通过 blink_stopped_sem
 * 信号量等待任务实际结束（超时 1 秒）。若超时，强制 vTaskDelete() 删除任务。
 *
 * @return esp_err_t 始终返回 ESP_OK（错误仅通过 ESP_LOGW 记录，不影响返回值）
 * @note 若当前无闪烁任务在运行，函数为无操作，直接返回 ESP_OK。
 */
esp_err_t led_stop_blink(void)
{
    if (blink_task_handle == NULL) {
        return ESP_OK;  // 没有闪烁在运行
    }

    ESP_LOGI(TAG, "正在停止闪烁...");
    blink_running = false;

    // 等待闪烁任务真正退出（超时 1 秒）
    if (blink_stopped_sem != NULL) {
        if (xSemaphoreTake(blink_stopped_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "闪烁已停止");
        } else {
            ESP_LOGW(TAG, "停止闪烁超时，强制删除任务");
            vTaskDelete(blink_task_handle);
            blink_task_handle = NULL;
        }
    }

    return ESP_OK;
}
