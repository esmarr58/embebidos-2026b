// =====================================================================
//  TC4 - Ejemplo 3 (RESUELTO): multiplexeo gobernado por un esp_timer
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Que hace: un esp_timer PERIODICO llama a refresco_cb cada REFRESH_US.
//  Cada llamada pinta UN digito y alterna. app_main queda libre: aunque
//  haga un "trabajo pesado" de 300 ms, el display NO parpadea, porque el
//  ritmo del barrido lo marca el timer, no el bucle.
//
//  Cuenta: REFRESH_US = 2500 us -> cada digito se repinta cada 5 ms
//          -> 200 Hz por digito (muy por encima de ~60 Hz de fusion).
//  Monitor: cada segundo imprime cuantas veces corrio el callback (~400).
//
//  Subobjetivos: [1] salidas [2] codificar [3] timer [4] refrescar [5] valor
// =====================================================================
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"      // esp_rom_delay_us (espera activa, solo para el experimento)

static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
#define DIG_DECENAS   GPIO_NUM_10
#define DIG_UNIDADES  GPIO_NUM_11

static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define REFRESH_US  2500     // periodo del timer = tiempo encendido por digito

// Datos compartidos entre app_main (escribe) y el callback (lee).
// 'volatile': el compilador debe releerlo de memoria en cada acceso.
static volatile uint8_t  valor = 42;          // numero 00..99 que se muestra
static volatile uint32_t llamadas = 0;        // cuantas veces corrio el callback
static uint8_t digito_activo = 0;             // 0 = decenas, 1 = unidades (solo callback)

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
    }
}

// ---- [4] Refrescar: apagar -> escribir -> encender -> alternar ----
// Corre en la TAREA esp_timer (no es una ISR): gpio_set_level es valido.
// Debe ser CORTO: aqui no hay printf, ni esperas, ni calculos pesados.
static void refresco_cb(void *arg)
{
    gpio_set_level(DIG_DECENAS, 0);                    // apagar los dos
    gpio_set_level(DIG_UNIDADES, 0);

    uint8_t v = valor;                                 // una sola lectura
    if (digito_activo == 0) {
        escribir_segmentos(PATRON[v / 10]);            // escribir  [2]
        gpio_set_level(DIG_DECENAS, 1);                // encender
    } else {
        escribir_segmentos(PATRON[v % 10]);
        gpio_set_level(DIG_UNIDADES, 1);
    }
    digito_activo ^= 1;                                // alternar
    llamadas++;
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

    // ---- [3] Crear y arrancar el timer periodico ----
    const esp_timer_create_args_t args = {
        .callback = &refresco_cb,      // que funcion llamar
        .arg      = NULL,              // argumento (no lo usamos)
        .name     = "refresco"         // nombre (aparece en esp_timer_dump)
    };
    esp_timer_handle_t t_refresco;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t_refresco));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));

    printf("Ej3: refresco cada %d us por digito -> %d Hz por digito\n",
           REFRESH_US, 1000000 / (2 * REFRESH_US));

    // ---- [5] Actualizar el valor: app_main es libre de hacer otra cosa ----
    uint32_t antes = 0;
    while (1) {
        esp_rom_delay_us(300000);      // "trabajo pesado": 300 ms de espera ACTIVA
        vTaskDelay(pdMS_TO_TICKS(700));

        valor = (valor + 1) % 100;     // un byte: se escribe de un solo golpe
        uint32_t ahora = llamadas;     // solo LEEMOS el contador del callback
        uint32_t n = ahora - antes;    // (no lo ponemos en 0: eso seria una carrera)
        antes = ahora;
        printf("valor=%02d  llamadas al callback en ~1 s = %lu\n",
               valor, (unsigned long)n);
    }
}
