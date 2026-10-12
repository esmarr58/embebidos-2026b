// =====================================================================
//  TC4 - Ejemplo 1: UN digito de 7 segmentos (sin multiplexeo)
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Que hace: el digito de UNIDADES cuenta 0..9, un numero por segundo.
//  Su transistor (2N3904) queda SIEMPRE habilitado: aun no hay barrido.
//  Sirve para comprobar el cableado del bus a..g y la tabla de patrones
//  antes de multiplexar.
//
//  Etiquetas de subobjetivo (las mismas en los 5 ejemplos):
//   [1] Configurar salidas      [2] Codificar numero -> patron
//   [3] Crear/arrancar el timer [4] Refrescar: apagar-escribir-encender-alternar
//   [5] Actualizar el valor
//  (Este ejemplo solo usa [1], [2] y [5].)
// =====================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// ---- Bus de segmentos a..g (catodo comun: '1' enciende el segmento) ----
static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
#define DIG_DECENAS   GPIO_NUM_10   // base del 2N3904 de las decenas
#define DIG_UNIDADES  GPIO_NUM_11   // base del 2N3904 de las unidades

// ---- [2] Tabla de patrones: bit0 = a, bit1 = b, ..., bit6 = g ----
static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// [2] Carga un patron en el bus: el bit i decide el segmento i.
static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
    }
}

// [1] Un pin como salida, iniciando en 0 (apagado).
static void config_salida(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

void app_main(void)
{
    // ---- [1] Configurar salidas ----
    for (int i = 0; i < 7; i++) config_salida(SEG[i]);
    config_salida(DIG_DECENAS);
    config_salida(DIG_UNIDADES);

    // Un solo digito: su transistor queda saturado todo el tiempo.
    gpio_set_level(DIG_UNIDADES, 1);

    // ---- [5] Actualizar el valor (aqui, el mismo bucle lo hace) ----
    uint8_t n = 0;
    while (1) {
        escribir_segmentos(PATRON[n]);              // [2] numero -> patron
        printf("Ej1: muestro %d (patron 0x%02X)\n", n, PATRON[n]);
        n = (n + 1) % 10;
        vTaskDelay(pdMS_TO_TICKS(1000));            // 1 s por numero
    }
}
