#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include <esp_ota_ops.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"

#include <sys/param.h>
#include "esp_netif.h"
#include "esp_task_wdt.h"

#include "esp_http_server.h"
#include <esp_wifi.h>


static const char *TAG = "ota_srv";

httpd_handle_t server = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_index_html_end");

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

    ESP_LOGW(TAG, "Uploading %d bytes", remaining);

	const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);

    //esp_task_wdt_config_t wdt_config;
    //wdt_config.timeout_ms = 30 * 1000;
    //wdt_config.idle_core_mask = 3;
    //wdt_config.trigger_panic = true;
    //esp_task_wdt_reconfigure(&wdt_config);

    //ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, remaining, &ota_handle));

	while (remaining > 0) {
		int recv_len = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));

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
	}

    //wdt_config.timeout_ms = CONFIG_ESP_TASK_WDT_TIMEOUT_S;
    //esp_task_wdt_reconfigure(&wdt_config);

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

httpd_handle_t ota_service_start(void) {
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

void ota_service_stop(void) {
    if (server)
        httpd_stop(server);
}
