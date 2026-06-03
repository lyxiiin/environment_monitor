#ifndef __WIFI_MANAGER_H
#define __WIFI_MANAGER_H

#include <esp_err.h>
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#define MAX_WIFI_CONFIGS    10
#define MAX_SSID_LEN        32
#define MAX_PASS_LEN        64


#ifdef __cplusplus
extern "C"{
#endif
// ==================== 状态定义 ====================
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED
} wifi_state_t;
// ==================== 回调函数类型 ====================
typedef void (*wifi_connect_callback_t)(bool success, void* user_data);
typedef void (*wifi_disconnect_callback_t)(void* user_data);
typedef void (*wifi_ip_callback_t)(esp_ip4_addr_t* ip, void* user_data);
// ==================== 配置结构体 ====================
typedef struct {
    const char* ssid;
    const char* password;
    uint8_t max_retry;
    wifi_auth_mode_t auth_threshold;
    wifi_connect_callback_t on_connect;
    wifi_disconnect_callback_t on_disconnect;
    wifi_ip_callback_t on_got_ip;
    void* user_data;
} wifi_manager_config_t;
// ==================== 公共API ====================
/**
 * @brief 初始化WiFi管理模块
 * @param config 配置结构体指针
 * @return esp_err_t 错误代码
 */
esp_err_t wifi_manager_init(const wifi_manager_config_t* config);
/**
 * @brief 启动WiFi连接
 * @return esp_err_t 错误代码
 */
esp_err_t wifi_manager_connect(void);
/**
 * @brief 断开WiFi连接
 * @return esp_err_t 错误代码
 */
esp_err_t wifi_manager_disconnect(void);
/**
 * @brief 获取当前WiFi状态
 * @return wifi_state_t 当前状态
 */
wifi_state_t wifi_manager_get_state(void);
/**
 * @brief 获取当前IP地址
 * @param ip ip地址输出指针
 * @return esp_err_t 错误代码
 */
esp_err_t wifi_manager_get_ip(esp_ip4_addr_t* ip);
/**
 * @brief 检查是否已连接
 * @return true 已连接，false 未连接
 */
bool wifi_manager_is_connected(void);


#ifdef __cplusplus
}
#endif


#endif

