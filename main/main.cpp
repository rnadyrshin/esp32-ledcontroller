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
#include "dns_server.h"


#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <esp_wifi.h>

#define EXAMPLE_ESP_WIFI_AP_PASS    ""
#define EXAMPLE_ESP_WIFI_CHANNEL    7
#define EXAMPLE_MAX_STA_CONN        2

#define TIME_TO_DISABLE_AP          1130


/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/*DHCP server option*/
#define DHCPS_OFFER_DNS             0x02

static const char *TAG = "main";
static int s_retry_num = 0;

esp_netif_t *esp_netif_ap = NULL;
int connected_clients = 0;
int time_to_disable_ap_sec = TIME_TO_DISABLE_AP;

MatrixPanel_I2S_DMA *dma_display = nullptr;

char ap_ssid[32];

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

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
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Station started");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
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
/*
    ESP_LOGW(TAG, "Set up softAP with IP: %d.%d.%d.%d", 
        (uint8_t)ip_info.ip.addr,
        (uint8_t)(ip_info.ip.addr >> 8), 
        (uint8_t)(ip_info.ip.addr >> 16), 
        (uint8_t)(ip_info.ip.addr >> 24));
*/

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d", ssid, pass, EXAMPLE_ESP_WIFI_CHANNEL);

    return esp_netif_ap;
}
/*
esp_netif_t *wifi_init_sta(void)
{
    esp_netif_t *esp_netif_sta = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_sta_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_STA_SSID,
            .password = EXAMPLE_ESP_WIFI_STA_PASSWD,
            .scan_method = WIFI_ALL_CHANNEL_SCAN,
            .failure_retry_cnt = EXAMPLE_ESP_MAXIMUM_RETRY,
            // Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
            // If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
            // to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
            // WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.

            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config) );

    ESP_LOGI(TAG_STA, "wifi_init_sta finished.");

    return esp_netif_sta;
}
*/
/*
void softap_set_dns_addr(esp_netif_t *esp_netif_ap,esp_netif_t *esp_netif_sta)
{
    esp_netif_dns_info_t dns;
    esp_netif_get_dns_info(esp_netif_sta,ESP_NETIF_DNS_MAIN,&dns);
    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(esp_netif_ap));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(esp_netif_ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_ap, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(esp_netif_ap));
}
*/


// Build http header
const static char http_html_hdr[] =
		"HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";

// Build 404 header
const static char http_404_hdr[] =
"HTTP/1.1 404 Not Found\r\nContent-type: text/html\r\n\r\n";

// Build http body
const static char http_index_hml[] =
		"<html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\
		<title>Control</title><style>body{background-color:lightblue;font-size:24px;}</style></head>\
		<body><h1>Control</h1><a href=\"high\">ON</a><br><a href=\"low\">OFF</a></body></html>";

const static char style[] =
"<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px} input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777} #file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer} #bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px} form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center} .btn{background:#3498db;color:#fff;cursor:pointer}</style>";

const static char serverIndex[] = 
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
"<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
"<label id='file-input' for='file'>   Choose file...</label>"
"<input type='submit' class=btn value='Update'>"
"<br><br>"
"<div id='prg'></div>"
"<br><div id='prgbar'>"
"<div id='bar'></div>"
"</div>"
"<br>"
"</form>"
"<script> function sub(obj){ var fileName = obj.value.split('\\\\'); document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1]; }; $('form').submit(function(e){ e.preventDefault(); var form = $('#upload_form')[0]; var data = new FormData(form); $.ajax({ url: '/update', type: 'POST', data: data, contentType: false, processData:false, xhr: function() { var xhr = new window.XMLHttpRequest(); xhr.upload.addEventListener('progress', function(evt) { if (evt.lengthComputable) { var per = evt.loaded / evt.total; $('#prg').html('progress: ' + Math.round(per*100) + '%'); $('#bar').css('width',Math.round(per*100) + '%'); } }, false); return xhr; }, success:function(d, s) { console.log('success!')  }, error: function (a, b, c) { } }); }); </script>"
"<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px} input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777} #file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer} #bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px} form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center} .btn{background:#3498db;color:#fff;cursor:pointer}</style>";
// + style;
/*
static esp_err_t index_get_handler(httpd_req_t *req)
{
    ESP_LOGE(TAG, "Serve root");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, serverIndex, strlen(serverIndex));

    return ESP_OK;
}
*/
esp_err_t index_get_handler(httpd_req_t *req)
{
	httpd_resp_send(req, (const char *) index_html_start, index_html_end - index_html_start);
	return ESP_OK;
}


esp_err_t update_post_handler(httpd_req_t *req)
{
	char buf[2 * 1024];
	esp_ota_handle_t ota_handle;
	int remaining = req->content_len;

    ESP_LOGW(TAG, "remaining %d", remaining);

	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);

    ESP_LOGW(TAG, "OTA info readed");

    //ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, remaining, &ota_handle));

    ESP_LOGW(TAG, "OTA begin");

	while (remaining > 0) {
        ESP_LOGI(TAG, "recv");

		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        ESP_LOGI(TAG, "recv_len %d", recv_len);

		// Timeout Error: Just retry
		if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
			continue;

		// Serious Error: Abort OTA
		} else if (recv_len <= 0) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
			return ESP_FAIL;
		}

		// Successful Upload: Flash firmware chunk
		if (esp_ota_write(ota_handle, (const void *)buf, recv_len) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
			return ESP_FAIL;
		}
        else {
            //ESP_LOGW(TAG, "writen %d", recv_len);
        }

		remaining -= recv_len;
        //delay(1);
	}

	// Validate and switch to new OTA image and reboot
	if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK) {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
			return ESP_FAIL;
	}

    ESP_LOGW(TAG, "Firmware update complete, rebooting now!");
	httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

	vTaskDelay(500 / portTICK_PERIOD_MS);
	esp_restart();

	return ESP_OK;
}

httpd_uri_t index_get = {
	.uri	  = "/",
	.method   = HTTP_GET,
	.handler  = index_get_handler,
	.user_ctx = NULL
};

httpd_uri_t update_post = {
	.uri	  = "/update",
	.method   = HTTP_POST,
	.handler  = update_post_handler,
	.user_ctx = NULL
};


esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 2;
    config.lru_purge_enable = true;
    config.stack_size = 8 * 1024;
    //config.core_id = 1;
        
    ESP_LOGW(TAG, "Starting server on port: '%d'", config.server_port);
            
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGW(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &index_get);
		httpd_register_uri_handler(server, &update_post);
    }
            
    return server;
}

static void dhcp_set_captiveportal_url(void) {
    // get the IP of the access point to redirect to
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);
    
    ESP_LOGW(TAG, "Set up softAP with IP: %d.%d.%d.%d", 
        (uint8_t)ip_info.ip.addr,
        (uint8_t)(ip_info.ip.addr >> 8), 
        (uint8_t)(ip_info.ip.addr >> 16), 
        (uint8_t)(ip_info.ip.addr >> 24));    

    // turn the IP into a URI
    //char* captiveportal_uri = (char*) malloc(32 * sizeof(char));
    //assert(captiveportal_uri && "Failed to allocate captiveportal_uri");
    
    //snprintf(captiveportal_uri, 32, "http://%d.%d.%d.%d", 
    //    (uint8_t)ip_info.ip.addr,
    //    (uint8_t)(ip_info.ip.addr >> 8), 
    //    (uint8_t)(ip_info.ip.addr >> 16), 
    //    (uint8_t)(ip_info.ip.addr >> 24));

    //ESP_LOGW(TAG, "CaptivePortal URL: %s", captiveportal_uri);


    // get a handle to configure DHCP with
    //esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        
    // set the DHCP option 114
    //ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    //ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captiveportal_uri, strlen(captiveportal_uri)));
    //ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));

    start_webserver();

    // Start the DNS server that will redirect all queries to the softAP IP
    //dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    //start_dns_server(&config);
    
}

//Initialize NVS
void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// Initialize AP
void wifi_ap_start() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    esp_netif_ap = wifi_init_softap(ap_ssid, EXAMPLE_ESP_WIFI_AP_PASS);

    ESP_ERROR_CHECK(esp_wifi_start());

    dhcp_set_captiveportal_url();
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

    xTaskCreate(ap_disconnect_task, "AP_DISCONNECT", 4096, 0, 1, 0);

/*
    // Wait until either the connection is established (WIFI_CONNECTED_BIT) or
    // connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
    // The bits are set by event_handler() (see above)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    // xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually happened.
    if (bits & WIFI_CONNECTED_BIT) {
        //ESP_LOGI(TAG, "connected to ap SSID:%s password:%s", EXAMPLE_ESP_WIFI_STA_SSID, EXAMPLE_ESP_WIFI_STA_PASSWD); 
        //softap_set_dns_addr(esp_netif_ap, esp_netif_sta);
    } else if (bits & WIFI_FAIL_BIT) {
        //ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s", EXAMPLE_ESP_WIFI_STA_SSID, EXAMPLE_ESP_WIFI_STA_PASSWD);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        return;
    }
*/

    /* Set sta as the default interface */
    //esp_netif_set_default_netif(esp_netif_sta);

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
