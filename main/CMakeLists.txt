idf_component_register(SRCS "gps_tracker.c" "config.c" 
                           "modules/gps/gps_module.c"
                           "modules/lte/lte_module.c" 
                           "modules/mqtt/mqtt_module.c"
                           "modules/battery/battery_module.c"
                    INCLUDE_DIRS "." "modules"
                    REQUIRES driver esp_timer nvs_flash json)