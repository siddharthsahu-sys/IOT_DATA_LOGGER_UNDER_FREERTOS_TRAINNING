idf_component_register(SRCS "Exercise2.c" "app_main.c"
                    PRIV_REQUIRES mqtt esp_partition nvs_flash esp_netif app_update
                    REQUIRES driver
                    INCLUDE_DIRS ".")
