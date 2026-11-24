/*
 * ble_serial_core.h - interfaz interna entre el nucleo comun y los backends BLE.
 * No es parte de la API publica.
 *
 * El nucleo (ble_serial_core.c, C puro) tiene los buferes circulares, el mutex,
 * el troceo por MTU-3, el reensamblado por '\n' y la tarea de envio.
 * Cada backend (NimBLE en ESP-IDF, Bluedroid/BLEDevice en Arduino) solo levanta el
 * servicio NUS, avisa los eventos al nucleo y envia un trozo cuando se le pide.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Configuracion: Kconfig en ESP-IDF; en Arduino se puede cambiar con -D ------- */
#ifndef CONFIG_BLE_SERIAL_TX_BUFFER_SIZE
#define CONFIG_BLE_SERIAL_TX_BUFFER_SIZE 2048
#endif
#ifndef CONFIG_BLE_SERIAL_RX_BUFFER_SIZE
#define CONFIG_BLE_SERIAL_RX_BUFFER_SIZE 2048
#endif
#ifndef CONFIG_BLE_SERIAL_MAX_LINE
#define CONFIG_BLE_SERIAL_MAX_LINE 256
#endif
#ifndef CONFIG_BLE_SERIAL_TX_TIMEOUT_MS
#define CONFIG_BLE_SERIAL_TX_TIMEOUT_MS 0
#endif
#ifndef CONFIG_BLE_SERIAL_LOCAL_MTU
#define CONFIG_BLE_SERIAL_LOCAL_MTU 517
#endif
#ifndef CONFIG_BLE_SERIAL_TASK_STACK
#define CONFIG_BLE_SERIAL_TASK_STACK 4096
#endif
#ifndef CONFIG_BLE_SERIAL_TASK_PRIORITY
#define CONFIG_BLE_SERIAL_TASK_PRIORITY 5
#endif

#define BSC_MIN_MTU   23
#define BSC_MAX_CHUNK (CONFIG_BLE_SERIAL_LOCAL_MTU - 3)
#define BSC_MAX_NAME  29 /* 31 bytes del scan response - 2 de cabecera AD */

typedef enum {
    BSC_SEND_OK = 0, /* el stack acepto el trozo */
    BSC_SEND_BUSY,   /* sin buferes / congestion: reintentar el mismo trozo mas tarde */
    BSC_SEND_FAIL    /* sin conexion o error: el nucleo revisa su estado */
} bsc_send_result_t;

/* ---- Lo implementa cada backend --------------------------------------------------- */

/* Levanta el stack, el servicio NUS y el advertising. name ya viene recortado. */
esp_err_t bsc_backend_start(const char *name);

/* Envia un trozo de len <= MTU-3 bytes como una notificacion de TX. */
bsc_send_result_t bsc_backend_send(const uint8_t *data, size_t len);

/* ---- Lo implementa el nucleo; lo llama el backend desde su tarea BLE ------------- */
void bsc_on_connect(void);
void bsc_on_disconnect(void);
void bsc_on_mtu(uint16_t mtu);
void bsc_on_subscribe(bool notify_enabled);
void bsc_on_rx(const uint8_t *data, size_t len);
void bsc_on_tx_ready(void); /* el stack libero buferes: reintentar ya */

#ifdef __cplusplus
}
#endif
