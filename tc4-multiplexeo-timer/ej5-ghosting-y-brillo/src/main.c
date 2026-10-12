// =====================================================================
//  TC4 - Ejemplo 5: ghosting y control de BRILLO por tiempo encendido
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  El timer ya no "pinta un digito por llamada": corre mas rapido
//  (TICK_US = 500 us) y divide el turno de cada digito en SLOTS = 10
//  rebanadas (10 x 500 us = 5 ms por digito -> 100 Hz por digito).
//    slot 0         : apagar -> escribir -> encender el digito que toca
//    slot == brillo : apagar (el resto del turno queda oscuro)
//    slot SLOTS-1   : pasar al otro digito
//  brillo = 1..10 slots encendido = ciclo de trabajo 10 %..100 % del turno.
//
//  GHOSTING: si ORDEN_CORRECTO = 0, se escriben los segmentos NUEVOS
//  mientras el digito ANTERIOR sigue encendido. Durante esos microsegundos
//  el digito anterior muestra el patron equivocado: aparece una "sombra"
//  tenue en segmentos que deberian estar apagados (se nota a oscuras).
//
//  Subobjetivos: [1] salidas [2] codificar [3] timer [4] refrescar [5] valor
// =====================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define ORDEN_CORRECTO  1        // 1 = apagar-escribir-encender; 0 = ghosting
#define TICK_US         500      // periodo del timer
#define SLOTS           10       // rebanadas por turno de digito

static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
static const gpio_num_t DIG[2] = { GPIO_NUM_10, GPIO_NUM_11 };  // decenas, unidades

static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// Compartidos: un byte cada uno (lectura/escritura de un solo golpe).
static volatile uint8_t valor  = 18;      // 00..99
static volatile uint8_t brillo = SLOTS;   // 1..SLOTS

// Estado privado del callback
static uint8_t digito = 0;
static uint8_t slot   = 0;

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
    }
}

// ---- [4] Refrescar con rebanadas de tiempo ----
static void tick_cb(void *arg)
{
    if (slot == 0) {
        uint8_t v = valor;
        uint8_t patron = (digito == 0) ? PATRON[v / 10] : PATRON[v % 10];
#if ORDEN_CORRECTO
        gpio_set_level(DIG[0], 0);           // 1) apagar los dos
        gpio_set_level(DIG[1], 0);
        escribir_segmentos(patron);          // 2) escribir
        gpio_set_level(DIG[digito], 1);      // 3) encender solo el que toca
#else
        escribir_segmentos(patron);          // MAL: el anterior sigue encendido
        gpio_set_level(DIG[digito ^ 1], 0);  //      y muestra el patron nuevo
        gpio_set_level(DIG[digito], 1);
#endif
    }
    if (slot == brillo) {                    // se acabo el tiempo encendido
        gpio_set_level(DIG[digito], 0);
    }
    if (++slot >= SLOTS) {                   // fin del turno: alternar
        slot = 0;
        digito ^= 1;
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

    // ---- [3] Timer periodico rapido (rebanadas de 500 us) ----
    const esp_timer_create_args_t args = {
        .callback = &tick_cb,
        .name     = "tick_display"
    };
    esp_timer_handle_t t_tick;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t_tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_tick, TICK_US));

    printf("Ej5: turno por digito = %d us -> %d Hz por digito, ORDEN_CORRECTO=%d\n",
           TICK_US * SLOTS, 1000000 / (2 * TICK_US * SLOTS), ORDEN_CORRECTO);

    // ---- [5] Actualizar el valor: rampa de brillo 1..10..1 ----
    int paso = -1;
    while (1) {
        uint8_t b = brillo + paso;
        if (b <= 1 || b >= SLOTS) paso = -paso;   // rebota en los extremos
        brillo = b;
        printf("brillo = %2d/%d slots (%3d %% del turno)\n",
               b, SLOTS, 100 * b / SLOTS);
        vTaskDelay(pdMS_TO_TICKS(400));
    }
}
