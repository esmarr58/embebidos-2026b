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

// PROGRAMA B: trabajo visible = conteo autonomo cada segundo.
// Una pulsacion solicita sleep; SOLTAR permite dormir; otra despierta.
// La pulsacion que despierta no solicita un segundo sleep.
#include "freertos/semphr.h"
#include "esp_sleep.h"

// Variante de CONTROL: mismos displays apagados, pero SIN dormir CPU.
// Seleccionar env:control-despierto en PlatformIO para comparar corriente.
#ifndef TC5_CONTROL_DESPIERTO
#define TC5_CONTROL_DESPIERTO 0
#endif
static bool contar = true;
static esp_timer_handle_t t_conteo, t_confirmacion;
static SemaphoreHandle_t callbacks_terminados;

static void conteo_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&candado);
    if (contar) cuenta = (cuenta + 1) % 100;
    portEXIT_CRITICAL(&candado);
}

static void confirmar_cb(void *arg)
{
    (void)arg;
    // No bloquea; se ejecuta en la misma tarea esp_timer que los otros callbacks.
    xSemaphoreGive(callbacks_terminados);
}

static uint8_t pausar_trabajo(void)
{
    portENTER_CRITICAL(&candado);
    contar = false;
    refrescar = false;
    apagar_display();
    uint8_t copia = cuenta;
    portEXIT_CRITICAL(&candado);
    ESP_ERROR_CHECK(esp_timer_stop(t_conteo));
    ESP_ERROR_CHECK(esp_timer_stop(t_refresco));
    // Esperar a que cualquier callback ya en curso termine antes de dormir.
    ESP_ERROR_CHECK(esp_timer_start_once(t_confirmacion, 1000));
    xSemaphoreTake(callbacks_terminados, portMAX_DELAY);
    return copia;
}

static void reanudar_trabajo(void)
{
    portENTER_CRITICAL(&candado);
    digito_activo = 0;
    refrescar = true;
    contar = true;
    portEXIT_CRITICAL(&candado);
    // El proximo incremento ocurre un segundo despues de reanudar.
    // No recuperar segundos perdidos: no es un reloj de hora real.
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_conteo, 1000000));
}

void app_main(void)
{
    configurar_hardware();
    callbacks_terminados = xSemaphoreCreateBinary();
    configASSERT(callbacks_terminados != NULL);
    const esp_timer_create_args_t cnt = {
        .callback = conteo_cb, .name = "conteo", .skip_unhandled_events = true
    };
    const esp_timer_create_args_t conf = {.callback = confirmar_cb, .name = "confirma"};
    ESP_ERROR_CHECK(esp_timer_create(&cnt, &t_conteo));
    ESP_ERROR_CHECK(esp_timer_create(&conf, &t_confirmacion));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_conteo, 1000000));
    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL));
    printf("\nTC05 B | ARRANQUE | cuenta=00 | modo=%s\n",
           TC5_CONTROL_DESPIERTO ? "CONTROL DESPIERTO" : "LIGHT SLEEP");
    armar_boton();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_ERROR_CHECK(gpio_intr_disable(BTN));
        if (leer_nivel_estable() != 0) { armar_boton(); continue; }
        printf("B | solicitud aceptada: SUELTA el boton para suspender\n");
        // Detener AHORA la cuenta y apagar: el numero no avanza al mantener.
        uint8_t guardada = pausar_trabajo();
        esperar_soltar(); // el despertar es por nivel BAJO, no por flanco
        printf("B | %s | cuenta_guardada=%02u | displays OFF | sin timer de despertar\n",
               TC5_CONTROL_DESPIERTO ? "CONTROL DESPIERTO" : "ENTRA SLEEP",
               (unsigned)guardada);
        fflush(stdout); // UART se drena al entrar a light sleep

        int64_t inicio = esp_timer_get_time();
        esp_err_t resultado = ESP_OK;
        esp_sleep_wakeup_cause_t causa = ESP_SLEEP_WAKEUP_UNDEFINED;
        if (TC5_CONTROL_DESPIERTO) {
            // Control experimental: no llamar esp_light_sleep_start().
            // Aqui se sondea SOLO para terminar el control al pulsar de nuevo.
            while (leer_nivel_estable() != 0) { }
        } else {
            ESP_ERROR_CHECK(gpio_wakeup_enable(BTN, GPIO_INTR_LOW_LEVEL));
            ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup());
            resultado = esp_light_sleep_start(); // CONTINUA aqui al despertar
            causa = esp_sleep_get_wakeup_cause();
            ESP_ERROR_CHECK(gpio_wakeup_disable(BTN));
            ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO));
        }
        int64_t tiempo_ms = (esp_timer_get_time() - inicio) / 1000;
        printf("B | %s | retorno=%s | causa=%s | tiempo_ms=%" PRId64
               " | cuenta_reanudada=%02u\n",
               TC5_CONTROL_DESPIERTO ? "FIN CONTROL" : "SALE SLEEP",
               esp_err_to_name(resultado),
               causa == ESP_SLEEP_WAKEUP_GPIO ? "GPIO" : "NO GPIO (ver retorno/modo)",
               tiempo_ms, (unsigned)guardada);
        // Incluso si sleep fue rechazado, recuperar salidas y no perder cuenta.
        reanudar_trabajo();
        armar_boton(); // consume liberacion del despertar ANTES de rearmar ISR
    }
}
