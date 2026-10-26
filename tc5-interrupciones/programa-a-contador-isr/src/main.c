// TC05 - Nombre Apellido - codigo - fecha
// ESP32-S3 / ESP-IDF 5.5.0 / espressif32@6.12.0.
// Montaje y patrones del TC4; GPIO1 (boton de P4) no ocupa segmentos.
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"

#define BTN GPIO_NUM_1
#define REFRESH_US 2500
#define REBOTE_US 30000
static const gpio_num_t SEG[7] = {4, 5, 6, 7, 15, 16, 17};
static const gpio_num_t DIG[2] = {10, 11}; // decenas, unidades
static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
static portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;
static uint8_t cuenta = 0, digito_activo = 0;
static bool refrescar = true;
static esp_timer_handle_t t_refresco;
static TaskHandle_t tarea_control;

// Solo llamar con candado tomado o antes de arrancar timers.
static void apagar_display(void)
{
    for (int i = 0; i < 2; ++i) gpio_set_level(DIG[i], 0);
    for (int i = 0; i < 7; ++i) gpio_set_level(SEG[i], 0);
}

// Igual que TC4: apagar -> patron -> un digito. TAREA esp_timer, NO ISR.
static void refresco_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&candado);
    if (refrescar) {
        for (int i = 0; i < 2; ++i) gpio_set_level(DIG[i], 0);
        uint8_t numero = digito_activo == 0 ? cuenta / 10 : cuenta % 10;
        uint8_t patron = PATRON[numero];
        for (int i = 0; i < 7; ++i) gpio_set_level(SEG[i], (patron >> i) & 1);
        gpio_set_level(DIG[digito_activo], 1);
        digito_activo ^= 1;
    }
    portEXIT_CRITICAL(&candado);
}

// Esta SI es ISR de GPIO: solo notifica. Sin printf, delay ni sleep.
static void boton_isr(void *arg)
{
    (void)arg;
    BaseType_t despertar = pdFALSE;
    vTaskNotifyGiveFromISR(tarea_control, &despertar);
    if (despertar) portYIELD_FROM_ISR();
}

// Antirrebote en TAREA: exige un nivel estable en muestras cada tick RTOS.
// Si se mantiene pulsado, la espera de soltar impide contar otra vez.
static int leer_nivel_estable(void)
{
    int anterior = gpio_get_level(BTN);
    int64_t desde = esp_timer_get_time();
    for (;;) {
        vTaskDelay(1); // al menos UN tick, tambien si configTICK_RATE_HZ=100
        int nivel = gpio_get_level(BTN);
        int64_t ahora = esp_timer_get_time();
        if (nivel != anterior) { anterior = nivel; desde = ahora; }
        if (ahora - desde >= REBOTE_US) return nivel;
    }
}

static void esperar_soltar(void)
{
    while (leer_nivel_estable() == 0) { /* cede CPU dentro del helper */ }
}

static void armar_boton(void)
{
    esperar_soltar();
    // Descarta notificaciones del rebote anterior ANTES de habilitar.
    ulTaskNotifyTake(pdTRUE, 0);
    // gpio_wakeup_enable cambia el tipo a NIVEL: restaurar el FLANCO.
    ESP_ERROR_CHECK(gpio_set_intr_type(BTN, GPIO_INTR_NEGEDGE));
    ESP_ERROR_CHECK(gpio_intr_enable(BTN));
}

static void configurar_hardware(void)
{
    tarea_control = xTaskGetCurrentTaskHandle();
    for (int i = 0; i < 9; ++i) {
        gpio_num_t pin = i < 7 ? SEG[i] : DIG[i - 7];
        ESP_ERROR_CHECK(gpio_reset_pin(pin));
        ESP_ERROR_CHECK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(pin, 0));
        // Mantener salidas de GPIO normales durante light sleep.
        ESP_ERROR_CHECK(gpio_sleep_sel_dis(pin));
    }
    const gpio_config_t entrada = {
        .pin_bit_mask = 1ULL << BTN, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&entrada));
    ESP_ERROR_CHECK(gpio_install_isr_service(0)); // ISR sin exigir IRAM
    ESP_ERROR_CHECK(gpio_isr_handler_add(BTN, boton_isr, NULL));
    ESP_ERROR_CHECK(gpio_intr_disable(BTN));
    apagar_display();
    const esp_timer_create_args_t ref = {
        .callback = refresco_cb, .name = "refresco",
        .skip_unhandled_events = true
    };
    ESP_ERROR_CHECK(esp_timer_create(&ref, &t_refresco));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));
}

// PROGRAMA A: no hay timer de conteo; SOLO la pulsacion incrementa.
void app_main(void)
{
    configurar_hardware();
    printf("\nTC05 A | ARRANQUE | cuenta=00 | BTN GPIO1 | refresco=2500 us\n");
    armar_boton();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // se desbloquea por ISR
        ESP_ERROR_CHECK(gpio_intr_disable(BTN));
        if (leer_nivel_estable() == 0) {
            portENTER_CRITICAL(&candado);
            cuenta = (cuenta + 1) % 100;
            uint8_t copia = cuenta;
            portEXIT_CRITICAL(&candado);
            printf("A | pulsacion aceptada | cuenta=%02u\n", (unsigned)copia);
        }
        armar_boton(); // esperar liberacion estable; no repetir al mantener
    }
}
