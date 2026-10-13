// =====================================================================
//  TC4 - Ejemplo 6 (RETO): contador 00..99 en el display multiplexado
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Que hace: los DOS digitos de 7 segmentos muestran 00, 01, 02 ... 99
//  y vuelven a 00; un numero por segundo. Todo con DOS esp_timer:
//   - t_refresco (periodico, REFRESH_US = 2500 us): refresco_cb pinta
//     UN digito y alterna (apagar -> escribir -> encender -> alternar).
//     Cada digito se repinta cada 5 ms -> 200 Hz por digito.
//   - t_segundo  (periodico, SEGUNDO_US = 1 000 000 us): segundo_cb
//     incrementa la cuenta y CODIFICA los dos patrones en pantalla[2].
//  app_main no barre nada: solo imprime la cuenta en el Monitor.
//
//  Datos compartidos: cuenta + pantalla[2] (tres bytes que deben cambiar
//  "de un golpe"). Se escriben y se leen dentro de una SECCION CRITICA
//  (portMUX_TYPE + portENTER_CRITICAL / portEXIT_CRITICAL), igual que en
//  el Ejemplo 4. Sin ella, al pasar de 39 a 40 se podria ver "49".
//
//  Subobjetivos: [1] salidas [2] codificar [3] timer [4] refrescar [5] valor
// =====================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
static const gpio_num_t DIG[2] = { GPIO_NUM_10, GPIO_NUM_11 };  // decenas, unidades

// ---- [2] Tabla de patrones: bit0 = a, bit1 = b, ..., bit6 = g ----
static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define REFRESH_US   2500        // periodo del refresco (tiempo por digito)
#define SEGUNDO_US   1000000     // periodo de la base de tiempo de 1 s

// ---- Datos compartidos + su candado ----
static uint8_t cuenta = 0;                             // 00..99
static uint8_t pantalla[2] = { 0x3F, 0x3F };           // "00" al arrancar
static portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;
static uint8_t digito_activo = 0;                      // solo lo usa refresco_cb

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
    }
}

// ---- [4] Refrescar: apagar -> escribir -> encender -> alternar ----
// Corre en la TAREA esp_timer (no es ISR). CORTO: sin printf ni esperas.
static void refresco_cb(void *arg)
{
    uint8_t patron;
    portENTER_CRITICAL(&candado);           // lectura corta y protegida
    patron = pantalla[digito_activo];
    portEXIT_CRITICAL(&candado);

    gpio_set_level(DIG[0], 0);              // apagar los dos
    gpio_set_level(DIG[1], 0);
    escribir_segmentos(patron);             // escribir
    gpio_set_level(DIG[digito_activo], 1);  // encender
    digito_activo ^= 1;                     // alternar
}

// ---- [5] Actualizar el valor: base de tiempo de 1 s ----
static void segundo_cb(void *arg)
{
    portENTER_CRITICAL(&candado);
    uint8_t c = cuenta;
    portEXIT_CRITICAL(&candado);

    c = (c + 1) % 100;                      // 99 -> 00
    uint8_t dec = PATRON[c / 10];           // [2] codificar FUERA del refresco
    uint8_t uni = PATRON[c % 10];

    portENTER_CRITICAL(&candado);           // los tres bytes "de un golpe"
    cuenta      = c;
    pantalla[0] = dec;
    pantalla[1] = uni;
    portEXIT_CRITICAL(&candado);
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

    // ---- [3] Crear y arrancar los DOS timers periodicos ----
    const esp_timer_create_args_t a_ref = {
        .callback = &refresco_cb,
        .name     = "refresco"
    };
    const esp_timer_create_args_t a_seg = {
        .callback = &segundo_cb,
        .name     = "segundo"
    };
    esp_timer_handle_t t_refresco, t_segundo;
    ESP_ERROR_CHECK(esp_timer_create(&a_ref, &t_refresco));
    ESP_ERROR_CHECK(esp_timer_create(&a_seg, &t_segundo));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_segundo, SEGUNDO_US));

    printf("Ej6: refresco %d us (%d Hz por digito), cuenta cada %d ms\n",
           REFRESH_US, 1000000 / (2 * REFRESH_US), SEGUNDO_US / 1000);

    // ---- app_main solo informa (printf aqui SI: es una tarea) ----
    uint8_t anterior = 255;
    while (1) {
        portENTER_CRITICAL(&candado);
        uint8_t c = cuenta;
        portEXIT_CRITICAL(&candado);
        if (c != anterior) {
            printf("cuenta = %02d\n", c);
            anterior = c;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
