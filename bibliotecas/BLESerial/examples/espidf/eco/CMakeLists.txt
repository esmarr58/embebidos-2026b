# Ejemplo ESP-IDF "eco" de BLESerial (proyecto independiente: idf.py build).
cmake_minimum_required(VERSION 3.16)
# La biblioteca es la carpeta BLESerial, tres niveles arriba de este archivo.
# Si la copiaste a components/BLESerial de tu proyecto, borra esta linea.
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ble_serial_eco)
