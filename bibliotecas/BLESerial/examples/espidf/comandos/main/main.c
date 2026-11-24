/*
 * comandos (ESP-IDF) - control con lineas clave=valor desde la app.
 *
 *   led=0 | led=1      apaga / enciende el LED en LED_GPIO
 *   periodo=<ms>       cada cuanto se envia la linea de estado (0 = no enviar)
 *
 * Responde "ok ..." o "error ...". La linea de estado (led:<0|1>,cuenta:<n>) es una
 * linea de datos que la app puede mostrar en un label o graficar.
 *
 * El LED de la ESP32-S3-DevKitC-1 es RGB direccionable (requiere el componente
 * led_strip); para no agregar dependencias este ejemplo usa un LED comun con su
 * resistencia en LED_GPIO.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "ble_serial.h"

#define LED_GPIO GPIO_NUM_2

static const char *TAG = "comandos";

static bool s_led = false;
static uint32_t s_periodo_ms = 1000;

/* Quita espacios al inicio y al final (modifica la cadena). */
static char *recortar(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
    return s;
}

static bool a_entero(const char *txt, uint32_t *valor)
{
    if (txt[0] == '\0' || txt[0] == '-') {
        return false;
    }
    char *fin = NULL;
    unsigned long v = strtoul(txt, &fin, 10);
    if (*fin != '\0') {
        return false;
    }
    *valor = (uint32_t)v;
    return true;
}

static void atender(char *linea)
{
    char *cmd = recortar(linea);
    if (cmd[0] == '\0') {
        return;
    }
    char *igual = strchr(cmd, '=');
    if (igual == NULL) {
        ble_serial_printf("error se esperaba clave=valor: %s\n", cmd);
        return;
    }
    *igual = '\0';
    char *clave = recortar(cmd);
    char *valor = recortar(igual + 1);
    uint32_t v = 0;

    if (strcmp(clave, "led") == 0) {
        if (strcmp(valor, "0") != 0 && strcmp(valor, "1") != 0) {
            ble_serial_printf("error led acepta 0 o 1: %s\n", valor);
            return;
        }
        s_led = (valor[0] == '1');
        gpio_set_level(LED_GPIO, s_led ? 1 : 0);
        ble_serial_printf("ok led=%d\n", s_led ? 1 : 0);
    } else if (strcmp(clave, "periodo") == 0) {
        if (!a_entero(valor, &v) || (v != 0 && (v < 50 || v > 60000))) {
            ble_serial_printf("error periodo en ms: 0 o 50..60000: %s\n", valor);
            return;
        }
        s_periodo_ms = v;
        ble_serial_printf("ok periodo=%lu\n", (unsigned long)s_periodo_ms);
    } else {
        ble_serial_printf("error comando desconocido: %s\n", clave);
    }
}

void app_main(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);

    ESP_ERROR_CHECK(ble_serial_init("ESP32-Comandos"));
    ESP_LOGI(TAG, "Comandos BLE listo");

    char linea[128];
    uint32_t cuenta = 0;
    TickType_t ultimo = xTaskGetTickCount();
    for (;;) {
        /* La biblioteca reensambla la linea aunque llegue partida en varias escrituras. */
        if (ble_serial_read_line(linea, sizeof(linea), pdMS_TO_TICKS(20)) >= 0) {
            atender(linea);
        }

        TickType_t ahora = xTaskGetTickCount();
        if (s_periodo_ms > 0 && (ahora - ultimo) >= pdMS_TO_TICKS(s_periodo_ms)) {
            ultimo = ahora;
            ble_serial_printf("led:%d,cuenta:%lu\n", s_led ? 1 : 0, (unsigned long)cuenta++);
        }
    }
}
