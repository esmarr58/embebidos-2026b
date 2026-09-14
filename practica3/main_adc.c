// =====================================================================
// Practica 3 - Contador optico 0-9 (fototransistor + display 7 segmentos)
// VERSION ANALOGICA: transistor en REGION ACTIVA -> ADC + umbral por software
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
//
// Portado a ESP-IDF (PlatformIO, framework = espidf) desde el sketch de
// Arduino "contador-region-activa.ino" (repositorio publico
// esmarr58/programas-optoelectronica, Practica 2 de Optoelectronica V3736).
//
// QUE HACE
//   Igual que la version digital (cuenta 0..9 los objetos que cruzan el haz
//   y los muestra en el display de catodo comun), pero AQUI la senal del
//   fototransistor NO viene cuadrada: se lee con el ADC (en milivoltios,
//   con calibracion) y el SOFTWARE decide si el haz esta presente o
//   interrumpido con un comparador de DOS umbrales (histeresis).
//
// POR QUE "REGION ACTIVA"
//   Con R_L pequena (o poca luz) el transistor NO satura: trabaja en la
//   region activa, donde V_CE = V_CC - I_C * R_L  e  I_C es proporcional a
//   la irradiancia. Entonces:
//     - haz PRESENTE      -> mucha I_C -> V_CE BAJO   -> pocos mV
//     - haz INTERRUMPIDO  -> I_C ~ 0   -> V_CE ~ V_CC -> ~3300 mV
//   El umbral lo pones TU en el codigo, y puedes verlo en el monitor
//   serie para calibrarlo (la practica pide medir V_out libre/bloqueado).
//
// HARDWARE (ESP32-S3-DevKitC-1)  -- todos los pines son SEGUROS
//   Segmentos a..g : GPIO 4, 5, 6, 7, 15, 16, 17  (cada uno -> R 220-330 -> segmento)
//   Comun display  : GND (catodo comun; un 1 ENCIENDE el segmento)
//   Sensor         : GPIO 8 = ADC1 canal 7  <- colector del fototransistor
//                    (mismo pin que usa contador-region-activa.ino de Opto)
//   Fototransistor : 3V3 -> R_L 2.2 k -> colector ; colector -> GPIO 8 ; emisor -> GND
//   LED emisor     : 5V -> R1 330 -> anodo ; catodo -> GND
//   ** Alimenta el fototransistor a 3.3 V, NO a 5 V: el ADC mide 0..3.3 V. **
//   Nota: los canales del ADC1 del ESP32-S3 son GPIO 1..10 (GPIO n = canal n-1).
//   El GPIO 18 de la version digital NO es de ADC1 (es ADC2, que comparte
//   hardware con Wi-Fi), por eso aqui se usa el GPIO 8.
//
// CALIBRACION (hazla una vez, en el monitor serie a 115200)
//   1) Anota los mV con el haz PRESENTE (valor bajo) y con el haz
//      INTERRUMPIDO (valor alto). El programa imprime ademas el minimo y
//      el maximo observados desde el arranque.
//   2) Pon UMBRAL_BAJO_MV un poco ARRIBA del "presente" y UMBRAL_ALTO_MV un
//      poco ABAJO del "interrumpido". Entre ambos el estado NO cambia: esa
//      banda es la histeresis y evita conteos falsos si la senal tiembla.
//
// Uso: copia este archivo en el src/main.c de tu PROYECTO BASE (no crees
//      un proyecto nuevo en clase) y presiona Build / Upload / Monitor.
// =====================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <stdio.h>
#include <stdbool.h>

// ---------------- Pines ----------------
static const int SEG[7] = { 4, 5, 6, 7, 15, 16, 17 };  // a,b,c,d,e,f,g
#define PIN_SENSOR      8                // GPIO 8 = ADC1 canal 7 (solo documental)
#define CANAL_ADC       ADC_CHANNEL_7    // ADC1_CH7 <-> GPIO 8 en ESP32-S3
#define ATENUACION_ADC  ADC_ATTEN_DB_12  // rango completo ~0..3.3 V
#define RESOLUCION_ADC  ADC_BITWIDTH_12  // 0..4095

// ---------------- Umbrales con HISTERESIS (en milivoltios) ----------------
// AJUSTALOS CON TU MEDICION. Valores iniciales tomados de la guia de la
// P2 de Opto (haz libre ~0.8-1.5 V, haz interrumpido ~3.3 V) y del sketch
// de Arduino (1400/2600 cuentas ~ 1.1/2.1 V).
#define UMBRAL_ALTO_MV  2500   // por ENCIMA de esto  = haz INTERRUMPIDO (objeto)
#define UMBRAL_BAJO_MV  1800   // por DEBAJO de esto  = haz PRESENTE (libre)
                               // (entre ambos se conserva el estado anterior)

// ---------------- Muestreo, antirrebote y traza ----------------
#define POLL_MS      10   // periodo de muestreo del ADC (tick FreeRTOS = 10 ms)
#define DEBOUNCE_MS  30   // el estado (ya con histeresis) debe mantenerse 30 ms
#define TRAZA_MS    250   // cada cuanto se imprime raw/mV para calibrar

// ---------------- Decodificador BCD -> 7 segmentos (Practica 1) ----------------
//     Catodo comun: un 1 ENCIENDE el segmento.  bit0=a, bit1=b, ... bit6=g
static const uint8_t DIGITO[10] = {
    0x3F, // 0 -> a b c d e f
    0x06, // 1 -> b c
    0x5B, // 2 -> a b d e g
    0x4F, // 3 -> a b c d g
    0x66, // 4 -> b c f g
    0x6D, // 5 -> a c d f g
    0x7D, // 6 -> a c d e f g
    0x07, // 7 -> a b c
    0x7F, // 8 -> todos
    0x6F  // 9 -> a b c d f g
};

// Configura los 7 pines de segmento como salida y los apaga (0 = apagado).
static void init_display(void) {
    for (int i = 0; i < 7; i++) {
        gpio_reset_pin(SEG[i]);
        gpio_set_direction(SEG[i], GPIO_MODE_OUTPUT);
        gpio_set_level(SEG[i], 0);
    }
}

// Muestra un digito (0..9): escribe cada segmento segun su bit.
static void mostrar_digito(uint8_t n) {
    if (n > 9) return;
    uint8_t m = DIGITO[n];
    for (int i = 0; i < 7; i++)
        gpio_set_level(SEG[i], (m >> i) & 1);   // bit i -> segmento i
}

// ---------------- ADC (oneshot + calibracion curve fitting) ----------------
static adc_oneshot_unit_handle_t adc_handle  = NULL;
static adc_cali_handle_t         cali_handle = NULL;

// Crea el esquema de calibracion (convierte cuentas -> mV con la curva
// de fabrica del chip). Devuelve false si no esta disponible.
static bool inicializar_calibracion(void) {
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ATENUACION_ADC,
        .bitwidth = RESOLUCION_ADC,
    };
    return adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK;
}

// Inicializa la unidad ADC1 en modo captura unica y configura el canal.
static void init_adc(void) {
    adc_oneshot_unit_init_cfg_t config_adc = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&config_adc, &adc_handle));

    adc_oneshot_chan_cfg_t config_canal = {
        .atten    = ATENUACION_ADC,
        .bitwidth = RESOLUCION_ADC,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, CANAL_ADC, &config_canal));

    if (!inicializar_calibracion()) {
        printf("Advertencia: sin calibracion; se usara mV ~ raw*3300/4095\n");
    }
}

// Lee el sensor: devuelve las cuentas crudas (0..4095) y los milivoltios.
// (Arduino: analogRead) -> adc_oneshot_read + adc_cali_raw_to_voltage
static void leer_sensor(int *raw, int *mv) {
    *raw = 0;
    *mv  = 0;
    if (adc_oneshot_read(adc_handle, CANAL_ADC, raw) != ESP_OK) return;
    if (cali_handle != NULL) {
        if (adc_cali_raw_to_voltage(cali_handle, *raw, mv) == ESP_OK) return;
    }
    *mv = (*raw * 3300) / 4095;   // aproximacion si no hay calibracion
}

void app_main(void) {
    init_display();
    init_adc();

    int cuenta = 0;
    mostrar_digito(cuenta);

    // Estado del comparador con histeresis (lectura "cruda" ya binarizada)
    int raw = 0, mv = 0;
    leer_sensor(&raw, &mv);
    bool interrumpido       = (mv > UMBRAL_ALTO_MV);   // estado inicial real
    bool interrumpido_prev  = interrumpido;            // para el antirrebote
    bool estado_confirmado  = interrumpido;            // ultimo estado confirmado
    int  estable_cnt        = 0;

    // Registro de mV minimo (haz libre) y maximo (haz bloqueado) observados
    int mv_min = mv, mv_max = mv;

    int64_t t_traza = esp_timer_get_time();   // us desde el arranque

    printf("Practica 3 - Contador optico 0-9 (MODO ADC: region activa)\n");
    printf("Sensor en GPIO %d (ADC1 canal %d), umbrales %d/%d mV, antirrebote %d ms. Cuenta = 0\n",
           PIN_SENSOR, (int)CANAL_ADC, UMBRAL_BAJO_MV, UMBRAL_ALTO_MV, DEBOUNCE_MS);
    printf("Calibra: observa los mV con haz presente vs interrumpido.\n");

    while (true) {
        // (a) Lectura del ADC en cuentas y en mV calibrados
        leer_sensor(&raw, &mv);
        if (mv < mv_min) mv_min = mv;
        if (mv > mv_max) mv_max = mv;

        // (b) Comparador con histeresis (dos umbrales)
        if (mv > UMBRAL_ALTO_MV)       interrumpido = true;    // se corto el haz
        else if (mv < UMBRAL_BAJO_MV)  interrumpido = false;   // el haz volvio
        // (entre UMBRAL_BAJO_MV y UMBRAL_ALTO_MV no cambia: histeresis)

        // (c) Antirrebote por polling (patron del TC2) sobre el estado binarizado
        if (interrumpido != interrumpido_prev) {
            estable_cnt = 0;
        } else {
            estable_cnt++;
        }
        interrumpido_prev = interrumpido;

        if (estable_cnt >= (DEBOUNCE_MS / POLL_MS) && interrumpido != estado_confirmado) {
            estado_confirmado = interrumpido;

            // (d) Flanco confirmado presente -> INTERRUMPIDO = +1 objeto
            if (estado_confirmado) {
                cuenta++;
                if (cuenta > 9) cuenta = 0;   // de 9 regresa a 0
                mostrar_digito(cuenta);
                printf("[%6lld ms] Objeto detectado (raw=%4d, %4d mV). Cuenta = %d\n",
                       (long long)(esp_timer_get_time() / 1000), raw, mv, cuenta);
            }
        }

        // (e) Traza periodica NO bloqueante para calibrar los umbrales
        //     (Arduino: millis() - tImpresion > 250)
        int64_t ahora = esp_timer_get_time();
        if (ahora - t_traza >= (int64_t)TRAZA_MS * 1000) {
            t_traza = ahora;
            printf("raw=%4d  V=%4d mV  | %-12s | min=%4d mV (libre)  max=%4d mV (bloqueado)\n",
                   raw, mv, estado_confirmado ? "INTERRUMPIDO" : "presente", mv_min, mv_max);
        }

        // (f) Ceder la CPU hasta la siguiente muestra
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
