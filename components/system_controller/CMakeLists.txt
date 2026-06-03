idf_component_register(
    SRCS "system_controller.c"
    INCLUDE_DIRS "."
    REQUIRES led SCD41 SHT40 i2c_driver wifi_manager mqtt_app_client nvs_flash sensor_protocol
)
