idf_component_register(SRCS "filters.c" "rtos_stats.c" "main.c"
                            "sx127x.c"
                            "sht3x.c"
                            "bmp180.c"
                            "bh1750.c"
                            "ds3231.c"
                            "mq135.c"
                            "mhz19.c"
                            "actuator.c"
                            "fsm.c"
                            "wifi_node.c"
                            "ota_update.c"
                            "ascon.c"
                            "crypto.c"
                            "packet.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_driver_i2c
                                     esp_driver_spi
                                     esp_driver_gpio
                                     esp_driver_uart
                                     esp_adc
                                     esp_timer
                                     esp_pm
                                     esp_hw_support
                                     esp_wifi
                                     esp_event
                                     esp_netif
                                     nvs_flash
                                     esp_https_ota
                                     esp_http_client
                                     app_update
                                     esp-tls
                                     mbedtls)
