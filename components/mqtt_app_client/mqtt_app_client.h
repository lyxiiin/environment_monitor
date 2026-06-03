#ifndef __MQTT_APP_CLIENT_H
#define __MQTT_APP_CLIENT_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif
// ==================== 回调函数 ====================
// 接收回调
typedef void (*mqtt_data_callback_t)(const char* topic, const char* data, int data_len, void* user_data);

typedef enum{
    MQTT_STATE_DISCONNECTED = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTING
} mqtt_app_client_state_t;

typedef struct {
    const char* broker_uri;      
    const char* username;
    const char* password;
    const char* client_id;
    const char* publish_topic;      // 发布主题
    const char* subscribe_topic;      // 订阅主题
    uint16_t    keepalive;
    int         qos;
    mqtt_data_callback_t on_data;     // 数据接收回调
    void*       user_data;             // 回调透传参数
} mqtt_app_client_config_t;
// ==================== 公共API ====================
/**
 * @brief 初始化MQTT客户端模块
 * @param config 配置结构体指针
 * @return esp_err_t 错误代码
 */
esp_err_t mqtt_app_client_init(mqtt_app_client_config_t *config);

/**
 * @brief 启动MQTT客户端模块
 * @return esp_err_t 错误代码
 */
esp_err_t mqtt_app_client_start(void);

/**
 * @brief 停止MQTT客户端模块
 * @return esp_err_t 错误代码
 */
esp_err_t mqtt_app_client_stop(void);

/**
 * @brief 发布MQTT消息
 * @param topic 主题
 * @param data 消息数据
 * @param qos QoS等级
 * @param retain 是否保留消息
 * @return esp_err_t 错误代码
 */
esp_err_t mqtt_app_client_publish(const char* topic, const char* data, int qos, int retain);

/**
 * @brief 订阅MQTT主题
 * @param topic 主题
 * @param qos QoS等级
 * @return esp_err_t 错误代码
 */
esp_err_t mqtt_app_client_subscribe(const char* topic, int qos);

/**
 * @brief 注册MQTT数据回调函数
 * @param callback 回调函数指针
 * @param user_data 用户数据指针
 * @return esp_err_t 错误代码
 */
esp_err_t mqtt_app_client_register_callback(mqtt_data_callback_t callback, void* user_data);

/**
 * @brief 检查MQTT客户端是否已连接
 * @return true 已连接
 * @return false 未连接
 */
bool mqtt_app_client_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
