idf_component_register(SRCS "Ethernet.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_netif esp_eth esp_event esp_driver_spi
                       PRIV_REQUIRES esp_driver_gpio lwip)
