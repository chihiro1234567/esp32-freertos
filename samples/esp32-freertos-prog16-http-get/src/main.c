#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "secret.h"

static const char *TAG = "httpget";

void wifi_init() {
    printf("WIFI_SSID = %s, WIFI_PASSWORD = %s, API_SERVER = %s\n", WIFI_SSID, WIFI_PASSWORD, API_SERVER);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ネットワークスタックの初期化
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_wifi_set_mode(WIFI_MODE_STA);

    // host name
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_set_hostname(netif, "esp32_s3_host1");
    }

    // wifi
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}


void get_wifi_infos() {
    ESP_LOGI(TAG, "=========================");
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi MAC Address: %02x:%02x:%02x:%02x:%02x:%02x", 
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGE(TAG, "** Failed to get MAC address: %s **", esp_err_to_name(ret));
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) {
        esp_netif_get_ip_info(netif, &ip_info);
        ESP_LOGI(TAG, "WiFi IP Address: %d.%d.%d.%d", 
            esp_ip4_addr1_16(&ip_info.ip),
            esp_ip4_addr2_16(&ip_info.ip),
            esp_ip4_addr3_16(&ip_info.ip),
            esp_ip4_addr4_16(&ip_info.ip));
    } else {
        ESP_LOGE(TAG, "** Failed to get IP address: Network interface not found **");
    }
    ESP_LOGE(TAG, "=========================");
}

// HTTP GETリクエストを行い、レスポンスを出力する
// esp_http_client_perform()を使ったハンドラ処理ではなく、
// open => read => closeを行うシンプルな通信処理
void http_get_task(void *pvParameters)
{
    char response_buffer[512] = {0};
    // APIの設定
    // 事前にHTTPサーバーを起動しておく
    esp_http_client_config_t config = {
        .url = API_SERVER,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .event_handler = NULL,
    };

    while(true){
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) {
            ESP_LOGE(TAG, "*** Failed to initialize HTTP connection ***");
            vTaskDelete(NULL);
            return;
        }
        esp_err_t ret = esp_http_client_open(client, 10000);
        if( ret < 0 ){
            ESP_LOGE(TAG, "*** HTTP CONNECTION ERROR. *** ret=%d", ret);
            vTaskDelete(NULL);
            return;
        }
        int header_status = esp_http_client_fetch_headers(client);
        if (header_status < 0) {
            ESP_LOGE(TAG, "*** Failed to fetch headers *** status=%d", header_status);
            vTaskDelete(NULL);
            return;
        }
        int status = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "http status = %d, content length = %d", status, content_length);
        int read_len = esp_http_client_read(client, response_buffer, sizeof(response_buffer)-1);
        ESP_LOGI(TAG, "read_len=%d, received data: %s", read_len, response_buffer);

        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        // wait time
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void app_main() {
    wifi_init();
    vTaskDelay(pdMS_TO_TICKS(3000));
    get_wifi_infos();
    xTaskCreate(http_get_task, "http_get_task", 4096, NULL, 5, NULL);
}
