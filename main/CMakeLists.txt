idf_component_register(SRCS "gps_tracker.c" "config.c" "version.c" "baud_rate_tester.c" "task_manager.c"
                           "modules/gps/gps_module.c"
                           "modules/gps/gps_debug.c"
                           "modules/lte/lte_module.c" 
                           "modules/mqtt/mqtt_module.c"
                           "modules/battery/battery_module.c"
                           "modules/modem_init/modem_init.c"
                    INCLUDE_DIRS "." "modules"
                    REQUIRES driver esp_timer nvs_flash json)