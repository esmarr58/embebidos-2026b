/*
 * ble_serial.h - API en C de BLESerial (puerto serie sobre BLE, perfil Nordic UART).
 *
 * Misma API en ESP-IDF (backend NimBLE) y en Arduino (backend Bluedroid del core).
 * En Arduino normalmente se usa el objeto BLESerial de <BLESerial.h>, que llama a estas
 * funciones por debajo.
 *
 * Reglas generales:
 *  - Todas las funciones son seguras entre tareas de FreeRTOS. Ninguna se puede llamar
 *    desde una ISR (devuelven 0 / error si se intenta).
 *  - Escribir nunca bloquea mas que el tiempo de espera de TX (por defecto 0 ms).
 *    El envio por radio lo hace una tarea propia de la biblioteca.
 *  - Lo recibido es un flujo de bytes: los limites de cada escritura BLE no significan
 *    nada. Para trabajar por lineas use ble_serial_read_line() o el callback de linea.
 *
 * Licencia MIT.
 */
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Version de la biblioteca */
#define BLE_SERIAL_VERSION_MAJOR 1
#define BLE_SERIAL_VERSION_MINOR 0
#define BLE_SERIAL_VERSION_PATCH 0

/* Nombre por defecto del dispositivo (contrato: "ESP32-Serial"). */
#ifdef CONFIG_BLE_SERIAL_DEFAULT_NAME
#define BLE_SERIAL_DEFAULT_NAME CONFIG_BLE_SERIAL_DEFAULT_NAME
#else
#define BLE_SERIAL_DEFAULT_NAME "ESP32-Serial"
#endif

/* UUIDs del Nordic UART Service (NUS), sin modificaciones. */
#define BLE_SERIAL_SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_SERIAL_RX_UUID      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E" /* app -> ESP32 */
#define BLE_SERIAL_TX_UUID      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E" /* ESP32 -> app */

/*
 * Callback de linea recibida. Se llama una vez por cada linea completa (terminada en
 * '\n'), ya reensamblada aunque haya llegado partida en varias escrituras BLE.
 * 'line' viene sin el '\n' ni el '\r' final y terminada en '\0'; solo es valida durante
 * la llamada. Si una linea excede CONFIG_BLE_SERIAL_MAX_LINE bytes se entrega en trozos.
 * Corre en la tarea del stack BLE: debe ser breve y no bloquear.
 * Mientras haya callback instalado, lo recibido NO se guarda en el bufer de lectura.
 */
typedef void (*ble_serial_rx_cb_t)(const char *line, size_t len, void *ctx);

/* ---- API del contrato ------------------------------------------------------------ */

/* Inicia BLE y el advertising. name = NULL usa BLE_SERIAL_DEFAULT_NAME.
 * El nombre se recorta a 29 bytes (lo que cabe en el scan response).
 * Devuelve ESP_ERR_INVALID_STATE si ya estaba iniciado. */
esp_err_t ble_serial_init(const char *name);

/* printf hacia BLE. Devuelve los bytes encolados (0 si se descartaron, -1 si error de formato). */
int ble_serial_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int ble_serial_vprintf(const char *fmt, va_list ap);

/* Encola len bytes. Devuelve los bytes encolados (0 si se descartaron por no haber conexion). */
size_t ble_serial_write(const void *data, size_t len);

/* Bytes disponibles para leer. */
size_t ble_serial_available(void);

/* Lee hasta maxlen bytes. Espera hasta timeout_ticks a que llegue al menos uno
 * (0 = no espera, portMAX_DELAY = espera indefinida). Devuelve los bytes leidos. */
size_t ble_serial_read(void *buf, size_t maxlen, TickType_t timeout_ticks);

/* true si hay un cliente conectado (enlace establecido). */
bool ble_serial_connected(void);

/* Instala (o quita con cb = NULL) el callback de linea recibida. */
void ble_serial_set_rx_callback(ble_serial_rx_cb_t cb, void *ctx);

/* true: lo que se imprime con ESP_LOGx tambien sale por BLE (ademas de la UART).
 * false: restaura la salida de log anterior. */
void ble_serial_redirect_log(bool enable);

/* ---- Extensiones (no estan en el contrato v1, no cambian su semantica) ---------- */

/* Lee una linea completa (sin '\n' ni '\r'), terminada en '\0'. Espera hasta
 * timeout_ticks a que haya una linea; si el bufer de lectura se llena sin '\n'
 * entrega lo que haya. Devuelve la longitud o -1 si no hubo linea a tiempo. */
int ble_serial_read_line(char *buf, size_t maxlen, TickType_t timeout_ticks);

/* Siguiente byte sin sacarlo, o -1 si no hay. */
int ble_serial_peek(void);

/* true si el cliente activo las notificaciones de TX (escribio el CCCD). */
bool ble_serial_subscribed(void);

/* MTU negociado (23 si no hay conexion o no se negocio). Cada notificacion lleva MTU-3. */
uint16_t ble_serial_mtu(void);

/* Bytes descartados por bufer TX lleno (se descartan los mas antiguos). */
uint32_t ble_serial_dropped(void);

/* Bytes descartados por bufer RX lleno: la escritura BLE que no cabe completa se
 * descarta entera (nunca se trunca). */
uint32_t ble_serial_rx_dropped(void);

/* Espacio libre en el bufer TX. */
size_t ble_serial_tx_free(void);

/* Tiempo maximo que una escritura espera espacio en el bufer TX antes de
 * descartar lo mas antiguo. Por defecto CONFIG_BLE_SERIAL_TX_TIMEOUT_MS (0). */
void ble_serial_set_tx_timeout(uint32_t ms);

/* false (defecto): sin conexion los bytes se descartan.
 * true: se guardan en el bufer circular y salen al conectarse y suscribirse. */
void ble_serial_set_buffer_when_disconnected(bool enable);

/* Espera a que el bufer TX se vacie (o a que no haya cliente suscrito), como maximo
 * timeout_ticks. Devuelve true si quedo vacio. */
bool ble_serial_flush(TickType_t timeout_ticks);

#ifdef __cplusplus
}
#endif
