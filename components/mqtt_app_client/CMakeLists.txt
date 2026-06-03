idf_component_register(
    SRCS "mqtt_app_client.c"
    INCLUDE_DIRS "."
    REQUIRES mqtt nvs_flash
)