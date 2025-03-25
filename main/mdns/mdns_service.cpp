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
#include "mdns.h"
#include "mdns_service.h"

#include <esp_wifi.h>

#define EXAMPLE_MDNS_INSTANCE "MDNS_INSTANCE"

static const char *TAG = "mdns";

void mdns_start(char *hostname) {
    mdns_init();
    mdns_hostname_set(hostname);
    
    ESP_LOGI(TAG, "mdns hostname set to: [%s]", hostname);
    mdns_instance_name_set(EXAMPLE_MDNS_INSTANCE);

    //structure with TXT records
    mdns_txt_item_t serviceTxtData[3] = {
        {"board", "esp32"},
        {"u", "user"},
        {"p", "password"}
    };

    //initialize service
    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer", "_http", "_tcp", 80, serviceTxtData, 3));
    ESP_ERROR_CHECK(mdns_service_subtype_add_for_host("ESP32-WebServer", "_http", "_tcp", NULL, "_server"));
#if CONFIG_MDNS_MULTIPLE_INSTANCE
    ESP_ERROR_CHECK(mdns_service_add("ESP32-WebServer1", "_http", "_tcp", 80, NULL, 0));
#endif

#if CONFIG_MDNS_PUBLISH_DELEGATE_HOST
    char *delegated_hostname;
    if (-1 == asprintf(&delegated_hostname, "%s-delegated", hostname)) {
        abort();
    }

    mdns_ip_addr_t addr4, addr6;
    esp_netif_str_to_ip4("10.0.0.1", &addr4.addr.u_addr.ip4);
    addr4.addr.type = ESP_IPADDR_TYPE_V4;
    esp_netif_str_to_ip6("fd11:22::1", &addr6.addr.u_addr.ip6);
    addr6.addr.type = ESP_IPADDR_TYPE_V6;
    addr4.next = &addr6;
    addr6.next = NULL;
    ESP_ERROR_CHECK(mdns_delegate_hostname_add(delegated_hostname, &addr4));
    ESP_ERROR_CHECK(mdns_service_add_for_host("test0", "_http", "_tcp", delegated_hostname, 1234, serviceTxtData, 3));
    free(delegated_hostname);
#endif // CONFIG_MDNS_PUBLISH_DELEGATE_HOST

    //add another TXT item
    ESP_ERROR_CHECK(mdns_service_txt_item_set("_http", "_tcp", "path", "/foobar"));
    //change TXT item value
    ESP_ERROR_CHECK(mdns_service_txt_item_set_with_explicit_value_len("_http", "_tcp", "u", "admin", strlen("admin")));
}

void mdns_stop(void) {
    mdns_free();
}
