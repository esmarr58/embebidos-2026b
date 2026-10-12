// =====================================================================
//  TC4 - Ejemplo 2: DOS digitos multiplexados "a mano" con vTaskDelay
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Que hace: muestra 00..99 en dos digitos que COMPARTEN el bus a..g.
//  El bucle enciende un digito, espera, enciende el otro, espera...
//  Funciona, pero tiene tres problemas que el Ejemplo 3 resuelve:
//   (a) El bucle esta OCUPADO barriendo: cualquier otra cosa que metas
//       aqui (un printf largo, leer el ADC) alarga el barrido y parpadea.
//   (b) vTaskDelay cuenta en TICKS de FreeRTOS (por defecto 100 Hz =
//       10 ms). pdMS_TO_TICKS(5) = 0 ticks: no espera nada.
//   (c) El conteo de 1 s se hace contando vueltas: si el barrido cambia,
//       el "segundo" tambien cambia.
//
//  EXPERIMENTO: cambia T_DIGITO_MS a 30 (parpadeo visible) y luego a 5
//  (= 0 ticks: el bucle nunca cede la CPU y el Task Watchdog se queja).
//
//  Subobjetivos: [1] salidas  [2] codificar  [4] refrescar  [5] valor
// =====================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
#define DIG_DECENAS   GPIO_NUM_10
#define DIG_UNIDADES  GPIO_NUM_11

static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

// Tiempo que cada digito queda encendido (ms). Con 10 ms cada digito se
// repinta cada 20 ms -> 50 Hz: en el limite del parpadeo.
#define T_DIGITO_MS  10

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
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
    config_salida(DIG_DECENAS);
    config_salida(DIG_UNIDADES);

    printf("Ej2: tick de FreeRTOS = %d Hz -> 1 tick = %d ms\n",
           (int)configTICK_RATE_HZ, (int)(1000 / configTICK_RATE_HZ));
    printf("Ej2: T_DIGITO_MS = %d ms -> pdMS_TO_TICKS = %d ticks\n",
           T_DIGITO_MS, (int)pdMS_TO_TICKS(T_DIGITO_MS));

    uint8_t valor = 0;
    int vueltas = 0;
    // Vueltas que caben en 1 s (cada vuelta pinta los 2 digitos).
    const int VUELTAS_POR_SEGUNDO = 1000 / (2 * T_DIGITO_MS);

    while (1) {
        // ---- [4] Refrescar: DECENAS ----
        gpio_set_level(DIG_UNIDADES, 0);                 // apagar
        escribir_segmentos(PATRON[valor / 10]);          // escribir  [2]
        gpio_set_level(DIG_DECENAS, 1);                  // encender
        vTaskDelay(pdMS_TO_TICKS(T_DIGITO_MS));          // ...y esperar

        // ---- [4] Refrescar: UNIDADES ----
        gpio_set_level(DIG_DECENAS, 0);
        escribir_segmentos(PATRON[valor % 10]);
        gpio_set_level(DIG_UNIDADES, 1);
        vTaskDelay(pdMS_TO_TICKS(T_DIGITO_MS));

        // ---- [5] Actualizar el valor contando vueltas (fragil) ----
        if (++vueltas >= VUELTAS_POR_SEGUNDO) {
            vueltas = 0;
            valor = (valor + 1) % 100;
        }
    }
}
