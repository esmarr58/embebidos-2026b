// P5 - Nombre Apellido - codigo - fecha
// Base de 80 puntos: contador optico por interrupcion, 2 displays.
// ESP-IDF 5.5.0, PlatformIO espressif32@6.12.0. No Arduino ni ADC en esta base.
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "logica_optica.h"

#define PIN_SENSOR GPIO_NUM_8 // mismo colector que en la P3 ADC; ahora GPIO
#define CONFIRMAR_LIBRE_US 2000
#define CONFIRMAR_BLOQUEADO_US 2000
#define REFRESH_US 2500
#define LONGITUD_COLA 64
static const gpio_num_t SEG[7] = {4, 5, 6, 7, 15, 16, 17};
static const gpio_num_t DIG[2] = {10, 11};
static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
typedef struct {int64_t t_us; int nivel;} evento_t;
static QueueHandle_t cola;
static portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;
static uint32_t flancos, perdidos;
static uint8_t visible, digito;

// ISR: captura flanco, nivel e instante. No filtra, imprime ni espera.
// Sin ESP_INTR_FLAG_IRAM: no se promete capturar durante operaciones de flash.
static void sensor_isr(void *arg)
{
    (void)arg;
    BaseType_t despertar = pdFALSE;
    portENTER_CRITICAL_ISR(&candado);
    evento_t e = {.t_us = esp_timer_get_time(), .nivel = gpio_get_level(PIN_SENSOR)};
    ++flancos;
    if (xQueueSendFromISR(cola, &e, &despertar) != pdTRUE) ++perdidos;
    portEXIT_CRITICAL_ISR(&candado);
    if (despertar) portYIELD_FROM_ISR();
}

// Mismo multiplexeo de TC04/TC05. Corre en la TAREA esp_timer, no es ISR.
static void refresco_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&candado);
    gpio_set_level(DIG[0], 0); gpio_set_level(DIG[1], 0);
    uint8_t n = digito == 0 ? visible / 10 : visible % 10;
    for (int i = 0; i < 7; ++i) gpio_set_level(SEG[i], (PATRON[n] >> i) & 1);
    gpio_set_level(DIG[digito], 1);
    digito ^= 1;
    portEXIT_CRITICAL(&candado);
}

static void configurar_salidas(void)
{
    for (int i = 0; i < 9; ++i) {
        gpio_num_t p = i < 7 ? SEG[i] : DIG[i - 7];
        ESP_ERROR_CHECK(gpio_reset_pin(p));
        ESP_ERROR_CHECK(gpio_set_direction(p, GPIO_MODE_OUTPUT));
        ESP_ERROR_CHECK(gpio_set_level(p, 0));
    }
    const esp_timer_create_args_t args = {.callback = refresco_cb,
        .name = "display", .skip_unhandled_events = true};
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, REFRESH_US));
}

void app_main(void)
{
    configurar_salidas();
    cola = xQueueCreate(LONGITUD_COLA, sizeof(evento_t));
    configASSERT(cola != NULL);
    // Resistencia EXTERNA de carga 2.2 kohm a 3V3, como en la practica anterior.
    const gpio_config_t entrada = {.pin_bit_mask = 1ULL << PIN_SENSOR,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&entrada));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_SENSOR, sensor_isr, NULL));
    ESP_ERROR_CHECK(gpio_intr_disable(PIN_SENSOR));
    optico_t estado;
    // Crear estado y habilitar detector como una sola seccion frente a la ISR.
    portENTER_CRITICAL(&candado);
    optico_iniciar(&estado, gpio_get_level(PIN_SENSOR), esp_timer_get_time(),
                   CONFIRMAR_LIBRE_US, CONFIRMAR_BLOQUEADO_US);
    portEXIT_CRITICAL(&candado);
    ESP_ERROR_CHECK(gpio_set_intr_type(PIN_SENSOR, GPIO_INTR_ANYEDGE));
    ESP_ERROR_CHECK(gpio_intr_enable(PIN_SENSOR));
    printf("\nP5 OPTICO | ARRANQUE | sensor=GPIO8 | 0=libre 1=bloqueado\n");
    printf("Filtros: libre=%d us, bloqueado=%d us | refresco=%d us | cola=%d\n",
           CONFIRMAR_LIBRE_US, CONFIRMAR_BLOQUEADO_US, REFRESH_US, LONGITUD_COLA);
    printf("Empieza con haz LIBRE. Bloqueado al arrancar NO cuenta un objeto.\n");
    uint32_t perdidos_vistos = 0;
    uint64_t total_impreso = 0;
    int64_t informe_us = esp_timer_get_time();
    for (;;) {
        evento_t e;
        // La tarea se bloquea al esperar: no hay un bucle de sondeo de conteo.
        BaseType_t hay_evento = xQueueReceive(cola, &e, 1);
        portENTER_CRITICAL(&candado);
        uint32_t perdidas_actuales = perdidos;
        if (perdidas_actuales != perdidos_vistos) {
            xQueueReset(cola);
            optico_resincronizar(&estado, gpio_get_level(PIN_SENSOR), esp_timer_get_time());
            perdidos_vistos = perdidas_actuales;
            // Este ensayo ya no es fiable: el monitor conserva perdidos > 0.
        } else if (hay_evento == pdTRUE) {
            optico_flanco(&estado, e.nivel, e.t_us);
        } else if (uxQueueMessagesWaiting(cola) == 0) {
            // Confirmar el ultimo estado solo sin flancos por procesar y si
            // el GPIO sigue coincidiendo; evita confirmar una cola atrasada.
            int64_t ahora = esp_timer_get_time();
            if (gpio_get_level(PIN_SENSOR) == estado.bruto) optico_avanzar(&estado, ahora);
        }
        visible = (uint8_t)(estado.total % 100);
        uint32_t f = flancos, p = perdidos;
        portEXIT_CRITICAL(&candado);

        int64_t ahora = esp_timer_get_time();
        if (estado.total != total_impreso || ahora - informe_us >= 1000000) {
            printf("total=%" PRIu64 " | display=%02u | bruto=%d estable=%d armado=%d"
                   " | flancos=%" PRIu32 " rechazados=%" PRIu64
                   " repetidos=%" PRIu64 " perdidos=%" PRIu32 "\n",
                   estado.total, (unsigned)(estado.total % 100), estado.bruto,
                   estado.estable, estado.armado, f, estado.rechazados, estado.repetidos, p);
            total_impreso = estado.total;
            informe_us = ahora;
        }
        // TODO EXTRA (20 puntos opcionales): agregar una funcion propia
        // verificable conservando el conteo base. No poner printf en ISR.
    }
}
