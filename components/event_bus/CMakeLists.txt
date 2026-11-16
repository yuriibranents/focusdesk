idf_component_register(SRCS "event_bus.c" "fd_heartbeat.c"
                       INCLUDE_DIRS "include"
                       REQUIRES focusdesk_common
                       PRIV_REQUIRES freertos esp_timer log)
