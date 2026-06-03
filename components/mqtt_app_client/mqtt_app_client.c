#include "mqtt_app_client.h"
#include "esp_err.h"
#include "esp_log.h"
#include <mqtt_client.h>
#include "esp_log_level.h"
#include "freertos/event_groups.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portable.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_app_client_config_t s_config = {0};
static mqtt_app_client_state_t s_state = MQTT_STATE_DISCONNECTED;
static EventGroupHandle_t s_event_group = NULL;

static char  *s_reassemble_buf  = NULL;   // 重组缓冲区指针
static int    s_reassemble_len  = 0;       // 已接收的字节数
static int    s_reassemble_total = 0;      // 期望的总字节数
static char  *s_reassemble_topic = NULL;   // 保存的主题副本
static int    s_reassemble_topic_len = 0;  // 保存的主题长度

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_DISCONNECTED_BIT BIT1

static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data){
    ESP_LOGI(TAG, "MQTT事件处理程序: %d", event_id);
    switch(event_id){
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT已连接");
            s_state = MQTT_STATE_CONNECTED;
            xEventGroupSetBits(s_event_group, MQTT_CONNECTED_BIT);
            esp_mqtt_client_subscribe(s_client, s_config.subscribe_topic, s_config.qos);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT已断开");
            s_state = MQTT_STATE_DISCONNECTED;
            xEventGroupSetBits(s_event_group, MQTT_DISCONNECTED_BIT);
            break;
        case MQTT_EVENT_DATA: {
            esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
                
            // 打印主题
            ESP_LOGI(TAG, "MQTT收到数据, topic: %.*s, data_len: %d, total: %d, offset: %d",
                     event->topic_len, event->topic,
                     event->data_len, event->total_data_len, event->current_data_offset);
            if(event->current_data_offset == 0){
                if(s_reassemble_buf != NULL){
                    vPortFree(s_reassemble_buf);
                    s_reassemble_buf = NULL;
                }
                if(s_reassemble_topic != NULL){
                    vPortFree(s_reassemble_topic);
                    s_reassemble_topic = NULL;
                }
                s_reassemble_buf = (char*)pvPortMalloc(event->total_data_len);
                if(s_reassemble_buf == NULL){
                    ESP_LOGE(TAG, "无法分配重组缓冲区,需要%d字节", event->total_data_len);
                    s_reassemble_len  = 0;
                    s_reassemble_total = 0;
                    break;
                }
                if(event->topic_len > 0 && event->topic != NULL){
                    s_reassemble_topic = (char *)pvPortMalloc(event->topic_len);
                    if(s_reassemble_topic != NULL){
                        memcpy(s_reassemble_topic,event->topic,event->topic_len);
                        s_reassemble_topic[event->topic_len] = '\0';
                        s_reassemble_topic_len = event->topic_len;
                    }else{
                        ESP_LOGE(TAG, "无法分配主题缓冲区,需要%d字节", event->topic_len);
                    }
                }
                s_reassemble_len = 0;
                s_reassemble_total = event->total_data_len;
            }
            memcpy(s_reassemble_buf+event->current_data_offset,event->data,event->data_len);
            s_reassemble_len += event->data_len;
            if(s_reassemble_len == s_reassemble_total){
                // 所有数据已接收完毕，可以处理数据
                ESP_LOGI(TAG, "所有数据已接收完毕，可以处理数据");
                // 将接收到的数据以十六进制格式打印
                if (event->data_len > 0) {
                    // 每16字节打印一行
                    char hex_buf[64];
                    for (int offset = 0; offset < event->data_len; offset += 16) {
                        int pos = 0;
                        int chunk = (event->data_len - offset > 16) ? 16 : (event->data_len - offset);
                        for (int i = 0; i < chunk; i++) {
                            pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos,
                                            "%02X ", (unsigned char)event->data[offset + i]);
                        }
                        ESP_LOGI(TAG, "  [%04X]: %s", offset, hex_buf);
                    }
                }
                // 调用用户注册的数据回调
                if (s_config.on_data) {
                    s_config.on_data(s_reassemble_topic, s_reassemble_buf, s_reassemble_len, s_config.user_data);
                }
            }
            break;
        }
        case MQTT_EVENT_ERROR:{
            esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
            if(event->error_handle){
                switch(event->error_handle->error_type){
                    case MQTT_ERROR_TYPE_TCP_TRANSPORT:
                        ESP_LOGE(TAG, "MQTT传输错误， esp_tls:0x%x (%s), sock_errno %d",
                            event->error_handle->esp_tls_last_esp_err,
                            esp_err_to_name(event->error_handle->esp_tls_last_esp_err),
                            event->error_handle->esp_transport_sock_errno);
                        break;
                    case MQTT_ERROR_TYPE_CONNECTION_REFUSED:
                        ESP_LOGE(TAG, "MQTT连接被拒绝, 返回码: %d", event->error_handle->connect_return_code);
                        break;
                    default:
                        ESP_LOGE(TAG, "MQTT错误类型: %d", event->error_handle->error_type);
                        break;
                }
            }else{
                ESP_LOGE(TAG, "MQTT错误(无错误详情)");
            }
            break;
        }
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT订阅成功, msg_id: %d", ((esp_mqtt_event_handle_t)event_data)->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT发布完成, msg_id: %d", ((esp_mqtt_event_handle_t)event_data)->msg_id);
            break;
    }
}

esp_err_t mqtt_app_client_init(mqtt_app_client_config_t *config) {
    if(config == NULL){
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_config, config, sizeof(mqtt_app_client_config_t));
    s_event_group = xEventGroupCreate();
    if(s_event_group == NULL){
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_config_t cfg = {
        .broker = {
            .address = {
                .uri = s_config.broker_uri,
            },
        },
        .credentials = {
            .username = s_config.username,
            .client_id = s_config.client_id,
            .authentication = {
                .password = s_config.password,
            },
        },
        .session = {
            .last_will = {
                .qos = s_config.qos,
            },
            .keepalive = s_config.keepalive,
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        },
    };
    s_client = esp_mqtt_client_init(&cfg);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(
        s_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler, 
        NULL
    ));
    
    return ESP_OK;
}

esp_err_t mqtt_app_client_start(void){
    if(s_client == NULL){
        ESP_LOGE(TAG,"MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if(s_state == MQTT_STATE_CONNECTED){
        return ESP_OK;
    }
    if(s_state == MQTT_STATE_CONNECTING){
        ESP_LOGE(TAG,"MQTT客户端正在连接中");
        return ESP_OK;
    }
    s_state = MQTT_STATE_CONNECTING;
    esp_err_t ret = esp_mqtt_client_start(s_client);
    if(ret != ESP_OK){
        ESP_LOGE(TAG,"MQTT客户端启动失败: %s", esp_err_to_name(ret));
        s_state = MQTT_STATE_DISCONNECTED;
        return ret;
    }
    ESP_LOGI(TAG, "MQTT客户端已启动");
    return ESP_OK;
}

esp_err_t mqtt_app_client_stop(void){
    if(s_client == NULL){
        ESP_LOGE(TAG,"MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if(s_state == MQTT_STATE_DISCONNECTED){
        return ESP_OK;
    }
    mqtt_app_client_state_t prev_state = s_state;
    s_state = MQTT_STATE_DISCONNECTING;
    esp_err_t ret = esp_mqtt_client_stop(s_client);
    if(ret != ESP_OK){
        ESP_LOGE(TAG,"MQTT客户端停止失败: %s", esp_err_to_name(ret));
        s_state = prev_state;
        return ret;
    }
    s_state = MQTT_STATE_DISCONNECTED;
    if(s_reassemble_buf != NULL){
        vPortFree(s_reassemble_buf);
        s_reassemble_buf = NULL;
    }
    if(s_reassemble_topic != NULL){
        vPortFree(s_reassemble_topic);
        s_reassemble_topic = NULL;
    }
    s_reassemble_len  = 0;
    s_reassemble_total = 0;
    s_reassemble_topic_len = 0;
    ESP_LOGI(TAG, "MQTT客户端已停止");
    return ESP_OK;
}

esp_err_t mqtt_app_client_publish(const char* topic, const char* data, int qos, int retain){
    if(s_client == NULL){
        ESP_LOGE(TAG,"MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if(s_state != MQTT_STATE_CONNECTED){
        ESP_LOGE(TAG,"MQTT未连接，无法发布消息");
        return ESP_ERR_INVALID_STATE;
    }
    if(topic == NULL || data == NULL){
        ESP_LOGE(TAG,"发布参数无效: topic或data为空");
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_publish(s_client, topic, data, (int)strlen(data), qos, retain);
    if(msg_id < 0){
        if(msg_id == -2){
            ESP_LOGE(TAG,"MQTT发布失败: 发送队列已满");
        }else{
            ESP_LOGE(TAG,"MQTT发布失败, msg_id: %d", msg_id);
        }
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "MQTT发布成功, topic: %s, msg_id: %d", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_app_client_subscribe(const char *topic, int qos){
    if(s_client == NULL){
        ESP_LOGE(TAG,"MQTT客户端未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    if(s_state != MQTT_STATE_CONNECTED){
        ESP_LOGE(TAG,"MQTT未连接，无法订阅主题");
        return ESP_ERR_INVALID_STATE;
    }
    if(topic == NULL){
        ESP_LOGE(TAG,"订阅主题为空");
        return ESP_ERR_INVALID_ARG;
    }

    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    if(msg_id < 0){
        ESP_LOGE(TAG,"MQTT订阅失败, topic: %s, msg_id: %d", topic, msg_id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "MQTT订阅成功, topic: %s, msg_id: %d", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_app_client_register_callback(mqtt_data_callback_t callback, void* user_data){
    s_config.on_data = callback;
    s_config.user_data = user_data;
    ESP_LOGI(TAG, "MQTT数据回调已注册");
    return ESP_OK;
}

bool mqtt_app_client_is_connected(void){
    return s_state == MQTT_STATE_CONNECTED;
}
