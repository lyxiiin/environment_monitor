#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
static const char *TAG = "wifi station";

// ==================== 私有变量 ====================
static EventGroupHandle_t s_wifi_event_group;  // WiFi事件组句柄
static wifi_manager_config_t s_config = {0};
static wifi_state_t s_state = WIFI_STATE_DISCONNECTED;
static int s_retry_num = 0;
static esp_ip4_addr_t s_ip_addr = {0};
// ==================== 事件位定义 ====================
#define WIFI_CONNECTED_BIT BIT0    // 连接成功标志位
#define WIFI_FAIL_BIT      BIT1    // 连接失败标志位
// ==================== WiFi事件处理函数 ====================


static void wifi_event_handler(void* arg, esp_event_base_t event_base,int32_t event_id, void* event_data){
    if(event_base == WIFI_EVENT){
        switch(event_id){
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "正在连接AP...");
                esp_wifi_connect();
                s_state = WIFI_STATE_CONNECTING;
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "已断开AP");
                s_state = WIFI_STATE_DISCONNECTED;
                if(s_retry_num < s_config.max_retry){
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "第%d/%d次重试连接AP...", s_retry_num, s_config.max_retry);
                }else{
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    s_state = WIFI_STATE_FAILED;
                    ESP_LOGE(TAG, "已经达到最大重试次数，连接失败。");
                    if (s_config.on_disconnect) {
                        s_config.on_disconnect(s_config.user_data);
                    }
                }
                break;
            default:
                break;
        }
    }else if(event_base == IP_EVENT){
        switch(event_id){
            case IP_EVENT_STA_GOT_IP:{
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                s_ip_addr = event->ip_info.ip;
                s_retry_num = 0;
                s_state = WIFI_STATE_CONNECTED;
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                ESP_LOGI(TAG, "获取到IP:" IPSTR, IP2STR(&event->ip_info.ip));
                if (s_config.on_got_ip) {
                    s_config.on_got_ip(&s_ip_addr, s_config.user_data);
                }
                
                if (s_config.on_connect) {
                    s_config.on_connect(true, s_config.user_data);
                }
                break;
            }
            default:
                break;
        }
    }else{
        // 非WiFi相关事件信号。
        return;
    }
}
// ==================== 公共API实现 ====================
esp_err_t wifi_manager_init(const wifi_manager_config_t* config){
    if(config == NULL){
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_config, config, sizeof(wifi_manager_config_t));

    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret ==ESP_ERR_NVS_NEW_VERSION_FOUND){
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_wifi_event_group = xEventGroupCreate();
    if(s_wifi_event_group == NULL){
        return ESP_ERR_NO_MEM;
    }
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // 初始化wifi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        NULL
    ));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        NULL
    ));
    // 配置wifi参数
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };
    if(s_config.ssid){
        strncpy((char*)wifi_config.sta.ssid, s_config.ssid, sizeof(wifi_config.sta.ssid) - 1);
    }
    if(s_config.password){
        strncpy((char*)wifi_config.sta.password, s_config.password, sizeof(wifi_config.sta.password) - 1);
    }
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "WiFi初始化完成");
    return ESP_OK;
}

esp_err_t wifi_manager_connect(void){
    if(s_state == WIFI_STATE_CONNECTED){
        return ESP_OK;
    }
    s_retry_num = 0;
    s_state = WIFI_STATE_CONNECTING;
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );
    if(bits & WIFI_CONNECTED_BIT){
        ESP_LOGI(TAG, "已连接到AP");
        return ESP_OK;
    }else if(bits & WIFI_FAIL_BIT){
        ESP_LOGE(TAG, "连接AP失败");
        return ESP_FAIL;
    }else{
        ESP_LOGE(TAG, "未知事件");
        return ESP_FAIL;
    }
}

esp_err_t wifi_manager_disconnect(void){
    s_state = WIFI_STATE_DISCONNECTED;
    return esp_wifi_disconnect();
}
wifi_state_t wifi_manager_get_state(void){
    return s_state;
}
esp_err_t wifi_manager_get_ip(esp_ip4_addr_t* ip){
    if(ip == NULL || s_state != WIFI_STATE_CONNECTED){
        return ESP_ERR_INVALID_STATE;
    }
    *ip = s_ip_addr;
    return ESP_OK;
}
bool wifi_manager_is_connected(void){
    return s_state == WIFI_STATE_CONNECTED;
}


