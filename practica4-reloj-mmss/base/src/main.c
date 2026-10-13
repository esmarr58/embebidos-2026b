// P4 - Nombre Apellido - codigo - fecha      <-- (cambia esta linea: tu clave de identidad)
// =====================================================================
//  Practica 4 - RELOJ mm:ss en 4 displays de 7 segmentos (codigo BASE)
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Que hace: los 4 digitos muestran MM:SS (00:00 .. 99:59) y el reloj
//  avanza un segundo cada segundo. Todo con DOS esp_timer periodicos
//  INDEPENDIENTES:
//   - t_refresco (REFRESH_US = 2000 us): refresco_cb pinta UN digito y
//     pasa al siguiente (apagar -> escribir -> encender -> avanzar).
//     4 digitos x 2 ms = 8 ms por vuelta -> 125 Hz por digito.
//   - t_tick     (SEGUNDO_US = 1 000 000 us): tick_cb suma 1 s.
//  app_main (una tarea) sondea los dos botones cada 20 ms (antirrebote)
//  y, una vez por segundo, imprime en el Monitor la hora y dos MEDICIONES
//  de los timers: el periodo real entre dos tick_cb (us) y cuantos
//  refrescos hubo en el ultimo segundo (debe dar ~500).
//
//  Este es el 80 % que se te entrega FUNCIONANDO. Los comentarios
//  "TODO (tu modificacion)" marcan donde suele entrar la Parte 2 de la
//  guia (alarma, cuenta regresiva, dos puntos, gptimer, ...).
//
//  Subobjetivos: [1] salidas/entradas [2] codificar [3] timers
//                [4] refrescar [5] base de 1 s [6] botones [7] medir
// =====================================================================
#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// ---- Bus de 7 segmentos (a..g), COMPARTIDO por los 4 digitos.
//      Catodo comun: un '1' enciende el segmento (R de 220-330 ohm en serie).
static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};

// ---- Habilitacion de cada digito (base de un 2N3904 por R_B, bajo lado).
//      D0 = decenas de MM, D1 = unidades de MM, D2 = decenas de SS, D3 = unidades de SS
static const gpio_num_t DIG[4] = {
    GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13
};

// ---- Botones a GND con pull-up interna (activos en BAJO) ----
#define BTN_PAUSE    GPIO_NUM_1      // pausa / reanuda
#define BTN_RESET    GPIO_NUM_2      // regresa a 00:00

// ---- [2] Tabla de patrones: bit0 = a, bit1 = b, ..., bit6 = g ----
static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

#define N_DIG        4
#define REFRESH_US   2000            // periodo del refresco (tiempo por digito)
#define SEGUNDO_US   1000000         // periodo de la base de tiempo de 1 s
#define SONDEO_MS    20              // sondeo de botones = antirrebote
#define MAX_SEG      6000            // 100 min: tras 99:59 vuelve a 00:00

// ---- Datos compartidos entre los callbacks y app_main + su candado ----
static volatile int      segundos     = 0;     // 0 .. 5999 (MM:SS)
static volatile bool     corriendo    = true;  // el reloj avanza?
static volatile int64_t  t_tick_us    = 0;     // instante del ultimo tick_cb
static volatile int64_t  periodo_us   = 0;     // periodo MEDIDO entre dos tick_cb
static volatile uint32_t n_refrescos  = 0;     // cuantas veces corrio refresco_cb
static portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;

static uint8_t dig_activo = 0;                 // 0..3 (solo lo usa refresco_cb)

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) {
        gpio_set_level(SEG[i], (patron >> i) & 0x01);
    }
}

// ---- [2] Digito (0..9) que va en la posicion p para el tiempo s ----
//      p: 0 = MM decenas, 1 = MM unidades, 2 = SS decenas, 3 = SS unidades
static uint8_t digito_de(int p, int s)
{
    int mm = (s / 60) % 100;
    int ss = s % 60;
    switch (p) {
        case 0:  return mm / 10;
        case 1:  return mm % 10;
        case 2:  return ss / 10;
        default: return ss % 10;
    }
}

// ---- [4] Refresco: apagar -> escribir -> encender -> avanzar ----
// Corre en la TAREA esp_timer (no es ISR). CORTO: sin printf ni esperas.
static void refresco_cb(void *arg)
{
    int s = segundos;                          // UNA lectura de 32 bits (atomica)

    for (int i = 0; i < N_DIG; i++) {          // 1) apagar los 4 (anti-ghosting)
        gpio_set_level(DIG[i], 0);
    }
    uint8_t patron = PATRON[digito_de(dig_activo, s)];
    // TODO (tu modificacion): aqui puedes alterar 'patron' antes de pintarlo
    //   (p. ej. apagar el display en la alarma, o encender el punto decimal
    //    del digito D1 como "dos puntos" que parpadean).
    escribir_segmentos(patron);                // 2) escribir el patron
    gpio_set_level(DIG[dig_activo], 1);        // 3) encender SOLO ese digito
    dig_activo = (dig_activo + 1) % N_DIG;     // 4) avanzar: 0,1,2,3,0,...

    n_refrescos++;                             // [7] para medir la tasa real
}

// ---- [5] Base de tiempo de 1 s: avanza el reloj ----
static void tick_cb(void *arg)
{
    int64_t ahora = esp_timer_get_time();      // us desde el arranque

    portENTER_CRITICAL(&candado);
    if (t_tick_us != 0) {
        periodo_us = ahora - t_tick_us;        // [7] periodo real medido
    }
    t_tick_us = ahora;
    if (corriendo) {
        segundos = (segundos + 1) % MAX_SEG;   // 99:59 -> 00:00
        // TODO (tu modificacion): cuenta regresiva (segundos--), alarma
        //   al llegar a una hora fija, detenerse en 00:00, ...
    }
    portEXIT_CRITICAL(&candado);
}

static void config_salida(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    gpio_set_level(pin, 0);
}

static void config_entrada_pullup(gpio_num_t pin)
{
    gpio_reset_pin(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

void app_main(void)
{
    // ---- [1] Configurar salidas y entradas ----
    for (int i = 0; i < 7; i++)     config_salida(SEG[i]);
    for (int i = 0; i < N_DIG; i++) config_salida(DIG[i]);
    config_entrada_pullup(BTN_PAUSE);
    config_entrada_pullup(BTN_RESET);

    // ---- [3] Crear y arrancar los DOS timers periodicos ----
    const esp_timer_create_args_t a_ref = {
        .callback = &refresco_cb,
        .name     = "refresco"
    };
    const esp_timer_create_args_t a_tick = {
        .callback = &tick_cb,
        .name     = "tick"
    };
    esp_timer_handle_t t_refresco, t_tick;
    ESP_ERROR_CHECK(esp_timer_create(&a_ref, &t_refresco));
    ESP_ERROR_CHECK(esp_timer_create(&a_tick, &t_tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_tick, SEGUNDO_US));
    // TODO (tu modificacion): migrar la base de 1 s a gptimer (driver/gptimer.h)

    printf("Practica 4 - Reloj mm:ss. Refresco %d us (%d Hz por digito), base %d us\n",
           REFRESH_US, 1000000 / (N_DIG * REFRESH_US), SEGUNDO_US);

    // ---- [6] Botones por sondeo + [7] informe una vez por segundo ----
    // printf aqui SI se vale: app_main es una tarea, no un callback de timer.
    int prev_pause = 1, prev_reset = 1;        // 1 = suelto (pull-up)
    int prev_s = -1;
    uint32_t prev_ref = 0;
    int64_t  prev_t   = esp_timer_get_time();

    while (1) {
        int p = gpio_get_level(BTN_PAUSE);
        int r = gpio_get_level(BTN_RESET);

        if (p == 0 && prev_pause == 1) {       // flanco de bajada = se presiono
            portENTER_CRITICAL(&candado);
            corriendo = !corriendo;
            bool c = corriendo;
            portEXIT_CRITICAL(&candado);
            printf("Reloj %s\n", c ? "corriendo" : "en pausa");
        }
        if (r == 0 && prev_reset == 1) {
            portENTER_CRITICAL(&candado);
            segundos = 0;
            portEXIT_CRITICAL(&candado);
            printf("Reloj reiniciado a 00:00\n");
        }
        // TODO (tu modificacion): pulsacion larga, ajuste de la hora de inicio, ...
        prev_pause = p;
        prev_reset = r;

        // Copia de los datos compartidos (de un golpe, dentro del candado)
        portENTER_CRITICAL(&candado);
        int     s   = segundos;
        int64_t per = periodo_us;
        portEXIT_CRITICAL(&candado);

        int64_t ahora = esp_timer_get_time();
        if (s != prev_s || ahora - prev_t >= 1000000) {   // 1 linea por segundo
            uint32_t ref = n_refrescos;
            float dt_s = (ahora - prev_t) / 1e6f;
            float ref_por_s = (ref - prev_ref) / dt_s;
            printf("t = %02d:%02d | periodo 1 s medido = %lld us | refrescos/s = %.0f\n",
                   (s / 60) % 100, s % 60, (long long)per, ref_por_s);
            prev_s   = s;
            prev_ref = ref;
            prev_t   = ahora;
        }
        vTaskDelay(pdMS_TO_TICKS(SONDEO_MS));  // cede la CPU + antirrebote
    }
}
