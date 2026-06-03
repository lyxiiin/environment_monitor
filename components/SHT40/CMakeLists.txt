# SHT40 温湿度传感器组件
# 使用 managed_components/esp-idf-lib__sht4x 驱动

idf_component_register(
    SRCS "sht40.c"
    INCLUDE_DIRS "."
    REQUIRES esp-idf-lib__sht4x
)
