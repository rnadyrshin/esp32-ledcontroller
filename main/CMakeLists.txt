idf_component_register(
			SRC_DIRS "." ${SRCDIRS}
			INCLUDE_DIRS ${INCLUDEDIRS}
			REQUIRES 
            ESP32-HUB75-MatrixPanel-I2S-DMA 
            nvs_flash 
            spi_flash 
            spiffs 
            partition_table 
            lwip 
            json 
            http_parser 
            heap 
            fatfs 
            esp_wifi 
            esp_system 
            esp_partition 
            esp_netif 
            esp_http_server 
            esp_event esp_common 
            dns_server
            app_update
            EMBED_TXTFILES web/index.html
            )
