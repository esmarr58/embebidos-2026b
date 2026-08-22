// =====================================================================
// TC1 - Nivel 2: "PWM a mano" con esperas OCUPADAS (busy-wait)
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
//
// Controla el BRILLO variando el duty cycle a mano.  A proposito esta
// MAL hecho: el busy-wait quema el CPU al 100 % y obliga a apagar el
// watchdog (esp_task_wdt_deinit); si no, la placa se REINICIA.  Ese
// parche delata el problema.  La forma correcta es el periferico LEDC.
//
// Uso: copia este archivo en el src/main.c de tu PROYECTO BASE.
//      Reto: prueba DUTY_PCT = 10, 50 y 90 y observa el brillo.
// =====================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"

#define PIN_LED  12
#define FREQ_HZ  1000    // 1 kHz: el ojo ya no ve el parpadeo
#define DUTY_PCT 50      // <-- BRILLO: 0..100

void app_main(void) {
    esp_task_wdt_deinit();   // sin esto, el busy-wait REINICIA la placa
    gpio_reset_pin(PIN_LED);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    uint32_t periodo = 1000000 / FREQ_HZ;              // us
    uint32_t t_on  = periodo * DUTY_PCT / 100;
    uint32_t t_off = periodo - t_on;
    while (true) {
        gpio_set_level(PIN_LED, 1);
        int64_t t = esp_timer_get_time();
        while (esp_timer_get_time() - t < t_on)  { }   // espera OCUPADA
        gpio_set_level(PIN_LED, 0);
        t = esp_timer_get_time();
        while (esp_timer_get_time() - t < t_off) { }   // espera OCUPADA
    }
}
