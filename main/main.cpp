#include "ESP32-HUB75-MatrixPanel-I2S-DMA.h"

#define PANEL_RES_X 64    // Number of pixels wide of each INDIVIDUAL panel module. 
#define PANEL_RES_Y 32     // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 13      // Total number of panels chained one to another
#define PANEL_WIDTH     (PANEL_RES_X * PANEL_CHAIN)

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"

#include <sys/param.h>
#include "esp_netif.h"

#include "esp_http_server.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_wifi.h>

#include "mdns/mdns_service.h"
#include "ota/ota_service.h"

#define EXAMPLE_ESP_WIFI_AP_PASS    ""
#define EXAMPLE_ESP_WIFI_CHANNEL    7
#define EXAMPLE_MAX_STA_CONN        2

#define TIME_TO_DISABLE_AP          30

static const char *TAG = "main";
static int s_retry_num = 0;

esp_netif_t *esp_netif_ap = NULL;
int connected_clients = 0;
int time_to_disable_ap_sec = TIME_TO_DISABLE_AP;

MatrixPanel_I2S_DMA *dma_display = nullptr;

char ap_ssid[32];
uint8_t ap_ip[4];

/* FreeRTOS event group to signal when we are connected/disconnected */
static EventGroupHandle_t s_wifi_event_group;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" joined, AID=%d", MAC2STR(event->mac), event->aid);
        connected_clients++;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "Station "MACSTR" left, AID=%d, reason:%d", MAC2STR(event->mac), event->aid, event->reason);
        connected_clients--;
    }
}

esp_netif_t *wifi_init_softap(char *ssid, char *pass)
{
    esp_netif_t *esp_netif_ap = esp_netif_create_default_wifi_ap();

    wifi_config_t wifi_ap_config = {};
    memset(&wifi_ap_config, 0, sizeof(wifi_ap_config));
    int ssid_len = strlen(ssid);
    int pass_len = strlen(pass);

    memcpy(wifi_ap_config.ap.ssid, ssid, ssid_len);
    wifi_ap_config.ap.ssid_len = ssid_len;
    memcpy(wifi_ap_config.ap.password, pass, pass_len);
    wifi_ap_config.ap.channel = EXAMPLE_ESP_WIFI_CHANNEL;
    wifi_ap_config.ap.max_connection = EXAMPLE_MAX_STA_CONN;
#ifdef CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT
    wifi_ap_config.ap.authmode = WIFI_AUTH_WPA3_PSK;
    wifi_ap_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#else /* CONFIG_ESP_WIFI_SOFTAP_SAE_SUPPORT */
    wifi_ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
#endif
    
    wifi_ap_config.ap.pmf_cfg = {.required = false};

    if (pass_len == 0) {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
    ap_ip[0] = (uint8_t)ip_info.ip.addr;
    ap_ip[1] = (uint8_t)(ip_info.ip.addr >> 8); 
    ap_ip[2] = (uint8_t)(ip_info.ip.addr >> 16);
    ap_ip[3] = (uint8_t)(ip_info.ip.addr >> 24);

    ESP_LOGI(TAG, "Wifi SoftAP started. SSID:%s password:%s channel:%d IP:%d.%d.%d.%d", ssid, pass, EXAMPLE_ESP_WIFI_CHANNEL, ap_ip[0], ap_ip[1], ap_ip[2], ap_ip[3]);

    return esp_netif_ap;
}

void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void wifi_ap_start() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    esp_netif_ap = wifi_init_softap(ap_ssid, EXAMPLE_ESP_WIFI_AP_PASS);

    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_ap_stop() {
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(esp_netif_ap));
    esp_netif_destroy(esp_netif_ap);
    esp_netif_ap = NULL;
}

// Task to be created.
void ap_disconnect_task(void * pvParameters) {

    while (time_to_disable_ap_sec) {
        if (connected_clients) {
            time_to_disable_ap_sec = TIME_TO_DISABLE_AP;
        }

        //ESP_LOGW(TAG, "Time to stop AP: %d", time_to_disable_ap_sec);
        vTaskDelay(pdMS_TO_TICKS(1000));
        time_to_disable_ap_sec--;
    }

    ESP_LOGW(TAG, "Stopping AP (SSID: %s)", ap_ssid);
    mdns_stop();
    ota_service_stop();
    wifi_ap_stop();

    vTaskDelete(NULL);
}

extern "C" void app_main() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    ESP_LOGW(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(ap_ssid, sizeof(ap_ssid), "LED_CTRL_%02X%02X%02X", mac[3], mac[4], mac[5]);
    ESP_LOGW(TAG, "SSID: %s", ap_ssid);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    //Initialize NVS
    nvs_init();
    
    // Initialize event group
    s_wifi_event_group = xEventGroupCreate();
    
    /* Register Event handler */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_ap_start();
    mdns_start("led-ctrl");
    ota_service_start();

    xTaskCreate(ap_disconnect_task, "AP_DISCONNECT", 4096, 0, 1, 0);

    /* Enable napt on the AP netif */
    if (esp_netif_napt_enable(esp_netif_ap) != ESP_OK) {
        ESP_LOGE(TAG, "NAPT not enabled on the netif: %p", esp_netif_ap);
    }

    ESP_LOGE(TAG, "NEW FIRMWARE 008");

    HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
    mxconfig.driver = HUB75_I2S_CFG::FM6124;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_16M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_15M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    //mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_8M;

    mxconfig.clkphase = 0;
    mxconfig.double_buff = 1;

    mxconfig.setPixelColorDepthBits(3);
    mxconfig.min_refresh_rate = 200;//255;//100;

    dma_display = new MatrixPanel_I2S_DMA(mxconfig);

    dma_display->begin();
    dma_display->setBrightness8(255);
    dma_display->clearScreen();

    dma_display->setTextSize(3);     // size 1 == 8 pixels high
    dma_display->setTextWrap(false); // Don't wrap at end of line - will do ourselves

    dma_display->setTextColor(dma_display->color565(0xFF, 0x70, 0));

    bool finish = false;
    int idx = 0;
    int len_pix = 0;
    int idx_start = 0;

    while (1)
    {
        dma_display->clearScreen();
        finish = false;
        idx = idx_start;

        while (!finish)
        {
            dma_display->setCursor(idx, 5);
            len_pix = dma_display->print("Flipper Hackspace    ");
            len_pix *= 18;
            idx += len_pix;
            if (idx > PANEL_WIDTH)
                finish = true;
        }

        dma_display->flipDMABuffer();
        idx_start--;
        if (idx_start <= -len_pix)
            idx_start += len_pix;

        delay(25);
    }
}
