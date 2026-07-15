idf_component_register(
    SRCS "src/scd4x.c"
    REQUIRES esp_driver_i2c esp_driver_gpio
    INCLUDE_DIRS "include"
)
