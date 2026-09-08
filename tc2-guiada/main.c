// =====================================================================
// TC2 (GUIADA) - Antirrebote + PWM (MCPWM) para brillo de LED
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
//
// Un push button (con resistencia pull-down) cambia la INTENSIDAD de un
// LED mediante PWM. Cada pulsacion CONFIRMADA por antirrebote sube el
// brillo un escalon; al pasar del 100% regresa a 0% (ciclico).
//
//   - PWM por hardware con el periferico MCPWM (driver legacy driver/mcpwm.h),
//     igual que en la Teoria 10 (Clase 31) y la Practica 9.
//   - Antirrebote por polling con contador de estabilidad, igual que en
//     las Entradas Digitales (TC3) del curso.
//
// Hardware:
//   LED : GPIO 12 (MCPWM0A) -> R 220 ohm -> LED -> GND
//   BTN : 3V3 -> push button -> nodo; nodo -> GPIO 4; nodo -> R 10k -> GND
// =====================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "soc/mcpwm_periph.h"
#include "esp_task_wdt.h"
#include <stdio.h>

// ---------------- Pines ----------------
#define PIN_LED      12    // salida PWM (MCPWM0A)
#define PIN_BTN      4     // push button con pull-down externo

// ---------------- PWM ----------------
#define FREC_LED     5000  // Hz (1-5 kHz para brillo de LED)
#define PASO_DUTY    20    // % que sube el brillo por cada pulsacion

// ---------------- Antirrebote (polling) ----------------
#define POLL_MS      10    // periodo de muestreo del boton
#define DEBOUNCE_MS  30    // tiempo estable para confirmar un flanco

static inline void delay_ms(int ms) { vTaskDelay(ms / portTICK_PERIOD_MS); }

// Configura MCPWM: unidad 0, timer 0, salida A sobre PIN_LED
void init_led_pwm(void) {
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PIN_LED);
    mcpwm_config_t pwm_config;
    pwm_config.frequency    = FREC_LED;
    pwm_config.cmpr_a       = 0;                 // duty inicial 0 %
    pwm_config.cmpr_b       = 0;
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode    = MCPWM_DUTY_MODE_0; // activa en alto
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
}

// Fija el brillo en porcentaje (0.0 - 100.0)
void led_set_brillo(float duty_pct) {
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, duty_pct);
}

void app_main(void) {
    esp_task_wdt_deinit();

    // Boton como entrada con pull-down (reforzado por la R externa)
    gpio_reset_pin(PIN_BTN);
    gpio_set_direction(PIN_BTN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_BTN, GPIO_PULLDOWN_ONLY);

    // PWM del LED
    init_led_pwm();
    int duty = 0;
    led_set_brillo(duty);

    // Variables de antirrebote
    int btn_prev      = 0;   // estado anterior (para detectar cambios)
    int btn_debounce  = 0;   // ciclos que el estado lleva estable
    int btn_confirmed = 0;   // 1 = pulsacion ya contabilizada

    printf("TC2: boton (GPIO %d) controla el brillo del LED (GPIO %d) por PWM\n",
           PIN_BTN, PIN_LED);

    while (true) {
        int btn_now = gpio_get_level(PIN_BTN);

        // 1) Filtrado antirrebote: el estado debe permanecer estable
        if (btn_now != btn_prev) {
            btn_debounce = 0;          // cambio: reinicia el contador
        } else {
            btn_debounce++;            // mismo estado: acumula estabilidad
        }

        // 2) Flanco ascendente CONFIRMADO (suelto -> presionado)
        if (btn_debounce >= (DEBOUNCE_MS / POLL_MS)) {
            if (btn_now == 1 && btn_confirmed == 0) {
                duty += PASO_DUTY;          // sube un escalon de brillo
                if (duty > 100) duty = 0;   // al pasar del 100% regresa a 0
                led_set_brillo(duty);       // aplica el nuevo duty al LED
                printf("Pulsacion -> brillo = %d %%\n", duty);
                btn_confirmed = 1;          // no repetir mientras se mantiene
            } else if (btn_now == 0) {
                btn_confirmed = 0;          // boton liberado: listo para otra
            }
        }

        btn_prev = btn_now;
        delay_ms(POLL_MS);
    }
}
