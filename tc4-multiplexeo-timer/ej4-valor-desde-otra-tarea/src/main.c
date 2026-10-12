// =====================================================================
//  TC4 - Ejemplo 4: el numero lo produce OTRA TAREA (datos compartidos)
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Patron PRODUCTOR / CONSUMIDOR:
//   - Tarea "contador" (productor): calcula el numero 00..99 y lo
//     CODIFICA en un "buffer de pantalla" pantalla[2] (dos patrones).
//   - Callback del timer (consumidor): solo COPIA pantalla[] al bus.
//     Nada de divisiones, nada de printf: el callback es corto.
//
//  El buffer tiene DOS bytes. Si el productor escribe pantalla[0] y el
//  callback llega antes de que escriba pantalla[1], se ve un numero
//  mezclado (p. ej. al pasar de 39 a 40 se veria "49" un instante).
//  Por eso ambos acceden dentro de una SECCION CRITICA (spinlock).
//
//  EXPERIMENTO: pon CALLBACK_PESADO en 1. El callback hace un printf en
//  cada llamada (~3-4 ms a 115200 baud, mas que el periodo de 2.5 ms):
//  las llamadas se atrasan, el barrido se vuelve irregular y parpadea.
//
//  Subobjetivos: [1] salidas [2] codificar [3] timer [4] refrescar [5] valor
// =====================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define CALLBACK_PESADO  0       // 0 = correcto; 1 = experimento del mito

static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
static const gpio_num_t DIG[2] = { GPIO_NUM_10, GPIO_NUM_11 };  // decenas, unidades

static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define REFRESH_US  2500

// ---- Datos compartidos: buffer de pantalla + su candado ----
static uint8_t pantalla[2] = { 0x3F, 0x3F };          // "00" al arrancar
static portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;
static uint8_t digito_activo = 0;                      // solo lo usa el callback

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
    }
}

// ---- [4] Refrescar (consumidor): copia y pinta, nada mas ----
static void refresco_cb(void *arg)
{
    uint8_t patron;
    portENTER_CRITICAL(&candado);           // lectura corta y protegida
    patron = pantalla[digito_activo];
    portEXIT_CRITICAL(&candado);

    gpio_set_level(DIG[0], 0);              // apagar
    gpio_set_level(DIG[1], 0);
    escribir_segmentos(patron);             // escribir
    gpio_set_level(DIG[digito_activo], 1);  // encender
    digito_activo ^= 1;                     // alternar

#if CALLBACK_PESADO
    // MAL: printf dentro del callback periodico (bloquea varios ms)
    printf("callback: digito %d, patron 0x%02X, t = %lld us\n",
           digito_activo, patron, esp_timer_get_time());
#endif
}

// ---- [5] Actualizar el valor (productor) ----
static void tarea_contador(void *arg)
{
    uint8_t valor = 0;
    while (1) {
        // [2] Codificar FUERA del callback: aqui si hay tiempo.
        uint8_t dec = PATRON[valor / 10];
        uint8_t uni = PATRON[valor % 10];

        portENTER_CRITICAL(&candado);       // los dos bytes "de un golpe"
        pantalla[0] = dec;
        pantalla[1] = uni;
        portEXIT_CRITICAL(&candado);

        printf("contador: %02d\n", valor);  // printf aqui SI: es una tarea
        valor = (valor + 1) % 100;
        vTaskDelay(pdMS_TO_TICKS(250));     // 4 numeros por segundo
    }
}

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
    config_salida(DIG[0]);
    config_salida(DIG[1]);

    // ---- [3] Crear y arrancar el timer periodico ----
    const esp_timer_create_args_t args = {
        .callback = &refresco_cb,
        .name     = "refresco"
    };
    esp_timer_handle_t t_refresco;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t_refresco));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));

    // ---- [5] El productor es una tarea aparte (pila 3 KB, prioridad 5) ----
    xTaskCreate(tarea_contador, "contador", 3072, NULL, 5, NULL);

    // app_main ya no tiene nada que hacer: puede terminar.
    // (El timer y la tarea siguen vivos: no dependen de app_main.)
}
