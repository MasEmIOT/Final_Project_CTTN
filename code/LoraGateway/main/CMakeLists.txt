idf_component_register(SRCS "packet.c" "crypto.c" "ascon.c" "main.c"
                            "sx127x.c"
                            "wifi_sta.c"
                            "cpu_load.c"
                            "serial_cmd.c"
                            "node_store.c"
                            "http_server.c"
                            "ssd1306.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_spi
                                     esp_driver_gpio
                                     esp_driver_uart
                                     esp_driver_i2c
                                     esp_timer
                                     esp_hw_support
                                     esp_wifi
                                     esp_event
                                     esp_netif
                                     nvs_flash
                                     esp_http_server
                                     lwip
                                     mbedtls)
