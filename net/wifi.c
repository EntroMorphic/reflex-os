#include "reflex_wifi.h"

#include <string.h>

#include "esp_check.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "reflex_log.h"
#include "reflex_config.h"
#include "reflex_service.h"
#include "reflex_event.h"

#define REFLEX_WIFI_TAG "reflex.wifi"

static esp_netif_t *s_reflex_netif = NULL;
static bool s_connected = false;

static void reflex_wifi_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        REFLEX_LOGI(REFLEX_WIFI_TAG, "disconnected, retrying...");
        reflex_event_publish(REFLEX_EVENT_WIFI_DISCONNECTED, NULL, 0);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        s_connected = true;
        REFLEX_LOGI(REFLEX_WIFI_TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        reflex_event_publish(REFLEX_EVENT_WIFI_CONNECTED, NULL, 0);
        reflex_event_publish(REFLEX_EVENT_IP_ACQUIRED, &event->ip_info.ip, sizeof(event->ip_info.ip));
    }
}

static esp_err_t reflex_wifi_init(void *ctx)
{
    (void)ctx;
    char ssid[33];
    char password[65];
    
    ESP_RETURN_ON_ERROR(esp_netif_init(), REFLEX_WIFI_TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), REFLEX_WIFI_TAG, "event loop failed");
    
    s_reflex_netif = esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), REFLEX_WIFI_TAG, "wifi init failed");
    
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &reflex_wifi_handler,
                                                            NULL,
                                                            NULL),
                        REFLEX_WIFI_TAG, "wifi handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT,
                                                            IP_EVENT_STA_GOT_IP,
                                                            &reflex_wifi_handler,
                                                            NULL,
                                                            NULL),
                        REFLEX_WIFI_TAG, "ip handler failed");

    wifi_config_t wifi_config = {0};
    
    if (reflex_config_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK && strlen(ssid) > 0) {
        memcpy(wifi_config.sta.ssid, ssid, strlen(ssid));
        if (reflex_config_get_wifi_password(password, sizeof(password)) == ESP_OK) {
            memcpy(wifi_config.sta.password, password, strlen(password));
        }
        
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), REFLEX_WIFI_TAG, "set mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wifi_config), REFLEX_WIFI_TAG, "set config failed");
        REFLEX_LOGI(REFLEX_WIFI_TAG, "initialized with ssid=%s", ssid);
    } else {
        REFLEX_LOGW(REFLEX_WIFI_TAG, "no wifi credentials configured");
    }

    return ESP_OK;
}

static esp_err_t reflex_wifi_start(void *ctx)
{
    (void)ctx;
    char ssid[33];
    if (reflex_config_get_wifi_ssid(ssid, sizeof(ssid)) == ESP_OK && strlen(ssid) > 0) {
        return esp_wifi_start();
    }
    return ESP_OK;
}

static esp_err_t reflex_wifi_stop(void *ctx)
{
    (void)ctx;
    return esp_wifi_stop();
}

static reflex_service_status_t reflex_wifi_status(void *ctx)
{
    (void)ctx;
    return s_connected ? REFLEX_SERVICE_STATUS_STARTED : REFLEX_SERVICE_STATUS_STOPPED;
}

esp_err_t reflex_wifi_service_register(void)
{
    static reflex_service_desc_t desc = {
        .name = "wifi",
        .init = reflex_wifi_init,
        .start = reflex_wifi_start,
        .stop = reflex_wifi_stop,
        .status = reflex_wifi_status,
    };
    return reflex_service_register(&desc);
}
