# Componente ESP-IDF de BLESerial (backend NimBLE).
# Los archivos del backend Arduino se excluyen; quedan vacios fuera de Arduino de todos modos.
idf_component_register(
    SRCS "src/ble_serial_core.c" "src/ble_serial_nimble.c"
    INCLUDE_DIRS "src"
    REQUIRES bt
    PRIV_REQUIRES nvs_flash log freertos
)
