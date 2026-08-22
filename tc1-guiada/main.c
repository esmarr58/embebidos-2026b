// =====================================================================
// TC1 - Nivel 1: "Tu primer parpadeo" (blink que cede el CPU)
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
//
// Objetivo: comprobar que el SDK compila y flashea, y entender la
// frecuencia del parpadeo.  f = 1000 / (2 * SEMIPERIODO_MS)  [Hz]
//
// Uso: copia este archivo en el src/main.c de tu PROYECTO BASE
//      (no crees un proyecto nuevo en clase) y presiona Build/Upload.
// =====================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define PIN_LED        12    // GPIO donde conectaste el LED (uno libre)
#define SEMIPERIODO_MS 500   // <-- cambia esto para la frecuencia

void app_main(void) {
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    while (true) {
        gpio_set_level(PIN_LED, 1);                    // enciende
        vTaskDelay(SEMIPERIODO_MS / portTICK_PERIOD_MS);
        gpio_set_level(PIN_LED, 0);                    // apaga
        vTaskDelay(SEMIPERIODO_MS / portTICK_PERIOD_MS);
    }
}
