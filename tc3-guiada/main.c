// =====================================================================
// TC3 (guiada) - Lector ADC de un potenciometro de 10 kOhm mostrado en
//                un display de 7 segmentos (catodo comun)
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
// Sesion 10 - ciclo 2026B - ESP32-S3 + ESP-IDF (PlatformIO)
//
// QUE HACE: lee el cursor de un potenciometro de 10 kOhm con el ADC1 del
//   ESP32-S3 (adc_oneshot + calibracion curve_fitting, como en la Clase 9),
//   convierte el raw (0..4095) a mV y a un NIVEL 0..9, y muestra el nivel
//   en el display de 7 segmentos de la Practica 1. Al girar el pot, el
//   digito cambia; en el Monitor se ve raw / mV / nivel.
//
// ESCALERA DE DIFICULTAD (elige con #define NIVEL, mas abajo):
//   NIVEL 0 -> solo ADC: imprime raw, mV ideal y mV calibrado en el Monitor.
//   NIVEL 1 -> raw -> nivel 0..9 y lo muestra en el display.
//   NIVEL 2 -> estabilidad: promedio de N_MUESTRAS + HISTERESIS en la
//              frontera, para que el digito no "baile".
//   RETO    -> busca los "TODO (Reto)" al final del archivo.
//
// MAPA DE PINES (ESP32-S3-DevKitC-1):
//   Potenciometro 10k : extremo 1 -> 3V3 | extremo 2 -> GND
//                       cursor    -> GPIO10  (ADC1, ADC_CHANNEL_9)
//   Display cat. comun: a b c d e f g -> GPIO 4 5 6 7 15 16 17 (R 330 c/u)
//                       COM -> GND
//   El cursor DEBE ir a un GPIO de ADC1 (GPIO1..10) que no use el display:
//   GPIO4..7 son ADC1_CH3..CH6 pero aqui son SALIDAS del display, y GPIO3
//   es strapping. Queda GPIO10, el canal del curso (Clase 9).
//
// Uso: copia este archivo en el src/main.c de tu PROYECTO BASE (no crees
//      un proyecto nuevo en clase) y presiona Build / Upload / Monitor.
// =====================================================================
#include <stdio.h>                    // printf
#include "freertos/FreeRTOS.h"        // tipos y macros base de FreeRTOS
#include "freertos/task.h"            // vTaskDelay, pdMS_TO_TICKS
#include "driver/gpio.h"              // gpio_reset_pin, gpio_set_direction, gpio_set_level
#include "esp_adc/adc_oneshot.h"      // driver ADC oneshot (unidad, canal, lectura)
#include "esp_adc/adc_cali.h"         // API de calibracion (adc_cali_handle_t)
#include "esp_adc/adc_cali_scheme.h"  // esquema curve_fitting

// --- Escalera: 0 = solo ADC, 1 = display, 2 = promedio + histeresis --------
#define NIVEL           0

// --- ADC (los mismos valores de la Clase 9) ----------------------------------
#define CANAL_ADC       ADC_CHANNEL_9   // ADC1 canal 9 = GPIO10 (canal = GPIO - 1)
#define ATENUACION_ADC  ADC_ATTEN_DB_12 // rango hasta ~3.3 V (potenciometro)
#define PERIODO_MS      200             // 5 lecturas por segundo en el Monitor

// --- Nivel 2: estabilidad ------------------------------------------------------
#define N_MUESTRAS      16              // muestras que se promedian por lectura
#define HISTERESIS      20              // margen en unidades de raw (~16 mV)

// --- Display: segmentos a..g en pines SEGUROS (identicos a la Practica 1) -----
//     Evita: 0,3,45,46 (strapping) - 26-37 (flash/PSRAM) - 19,20 (USB)
static const int SEG[7] = { 4, 5, 6, 7, 15, 16, 17 };  // a,b,c,d,e,f,g

// --- Decodificador BCD -> 7 segmentos (catodo comun: 1 ENCIENDE) --------------
//     bit0=a, bit1=b, ... bit6=g
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

// --- Handles del driver (viven toda la ejecucion) ------------------------------
static adc_oneshot_unit_handle_t adc_handle  = NULL;   // "objeto" del ADC
static adc_cali_handle_t         cali_handle = NULL;   // "objeto" de la calibracion

// =====================================================================
//  ADC: configuracion (Clase 9, pasos 3 y 4)
// =====================================================================
bool inicializar_calibracion(void) {
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id  = ADC_UNIT_1,
        .atten    = ATENUACION_ADC,
        .bitwidth = ADC_BITWIDTH_12,
    };
    return adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) == ESP_OK;
}

void init_adc(void) {
    adc_oneshot_unit_init_cfg_t config_adc = {      // (a) que unidad
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&config_adc, &adc_handle); //     crea la unidad

    adc_oneshot_chan_cfg_t config_canal = {         // (b) como medir el canal
        .atten    = ATENUACION_ADC,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, CANAL_ADC, &config_canal);

    if (!inicializar_calibracion()) {               // (c) calibracion
        printf("Error al inicializar la calibracion\n");
    }
}

// =====================================================================
//  ADC: lectura y conversiones
// =====================================================================
// Una lectura cruda (0..4095). Es un conteo adimensional, NO un voltaje.
int leer_raw(void) {
    int valor_bruto = 0;
    adc_oneshot_read(adc_handle, CANAL_ADC, &valor_bruto);
    return valor_bruto;
}

// Promedio de n lecturas consecutivas (reduce el ruido visible; no cambia el LSB).
int leer_raw_promedio(int n) {
    int suma = 0;
    for (int i = 0; i < n; i++) {
        suma += leer_raw();
    }
    return suma / n;
}

// raw -> mV con la curva de fabrica; si no hay calibracion, modelo ideal.
int raw_a_mv(int raw) {
    int mv_calibrado;
    if (cali_handle != NULL &&
        adc_cali_raw_to_voltage(cali_handle, raw, &mv_calibrado) == ESP_OK) {
        return mv_calibrado;              // mV (curve fitting)
    }
    return 3300 * raw / 4096;             // mV, modelo ideal (multiplicar primero)
}

// raw -> nivel 0..9: 4096 niveles / 10 digitos = 409.6 raw por digito.
int raw_a_nivel(int raw) {
    int nivel = raw * 10 / 4096;          // multiplicar primero: sin division entera a cero
    if (nivel > 9) nivel = 9;             // por seguridad (raw = 4095 ya da 9)
    return nivel;
}

// Primer raw que pertenece a un nivel: ceil(nivel * 409.6).
int raw_inicio_nivel(int nivel) {
    return (nivel * 4096 + 9) / 10;
}

// Cambia de nivel solo si el raw cruzo la frontera con un margen HISTERESIS.
int nivel_con_histeresis(int raw, int nivel_actual) {
    int inferior = raw_inicio_nivel(nivel_actual);       // primer raw del nivel actual
    int superior = raw_inicio_nivel(nivel_actual + 1);   // primer raw del nivel siguiente
    if (raw >= superior + HISTERESIS) return raw_a_nivel(raw);   // subio con margen
    if (raw <  inferior - HISTERESIS) return raw_a_nivel(raw);   // bajo con margen
    return nivel_actual;                                         // zona muerta: no cambia
}

// =====================================================================
//  Display de 7 segmentos (identico a la Practica 1)
// =====================================================================
void init_display(void) {
    for (int i = 0; i < 7; i++) {
        gpio_reset_pin(SEG[i]);
        gpio_set_direction(SEG[i], GPIO_MODE_OUTPUT);
    }
}

void mostrar_digito(uint8_t n) {
    if (n > 9) return;
    uint8_t m = DIGITO[n];
    for (int i = 0; i < 7; i++)
        gpio_set_level(SEG[i], (m >> i) & 1);   // bit i -> segmento i
}

void apagar_display(void) {
    for (int i = 0; i < 7; i++)
        gpio_set_level(SEG[i], 0);
}

// =====================================================================
//  NIVEL 0: solo ADC al Monitor (raw, mV ideal, mV calibrado)
// =====================================================================
void nivel0_solo_adc(void) {
    while (1) {
        int raw      = leer_raw();
        int mv_ideal = 3300 * raw / 4096;     // modelo ideal (Clase 9)
        int mv_cal   = raw_a_mv(raw);         // curva de fabrica
        printf("raw=%4d  mV_ideal=%4d  mV_cal=%4d\n", raw, mv_ideal, mv_cal);
        vTaskDelay(pdMS_TO_TICKS(PERIODO_MS));
    }
}

// =====================================================================
//  NIVEL 1: raw -> nivel 0..9 en el display
// =====================================================================
void nivel1_display(void) {
    init_display();
    while (1) {
        int raw   = leer_raw();
        int nivel = raw_a_nivel(raw);
        mostrar_digito(nivel);
        printf("raw=%4d  mV=%4d  nivel=%d\n", raw, raw_a_mv(raw), nivel);
        vTaskDelay(pdMS_TO_TICKS(PERIODO_MS));
    }
}

// =====================================================================
//  NIVEL 2: promedio de N_MUESTRAS + histeresis (el digito no baila)
// =====================================================================
void nivel2_estable(void) {
    init_display();
    int nivel = 0;
    while (1) {
        int raw   = leer_raw_promedio(N_MUESTRAS);
        int nuevo = nivel_con_histeresis(raw, nivel);
        if (nuevo != nivel) {
            nivel = nuevo;
            printf("--- cambio de nivel -> %d ---\n", nivel);
        }
        mostrar_digito(nivel);
        printf("raw_prom=%4d  mV=%4d  nivel=%d\n", raw, raw_a_mv(raw), nivel);

        // TODO (Reto A): si raw >= 4090 (saturacion) haz PARPADEAR el digito 9
        //   (alterna mostrar_digito / apagar_display en lecturas sucesivas).
        // TODO (Reto B): en vez del nivel por raw, muestra las DECENAS de
        //   porcentaje del voltaje calibrado: pct10 = raw_a_mv(raw) / 330.
        //   Compara en el Monitor cuando difiere del nivel por raw.

        vTaskDelay(pdMS_TO_TICKS(PERIODO_MS));
    }
}

// =====================================================================
//  app_main: configurar una vez, luego el nivel elegido para siempre
// =====================================================================
void app_main(void) {
    init_adc();                       // una sola vez (unidad, canal, calibracion)
    printf("TC3 - lector ADC en GPIO10 - NIVEL %d\n", NIVEL);

#if NIVEL == 0
    nivel0_solo_adc();
#elif NIVEL == 1
    nivel1_display();
#else
    nivel2_estable();
#endif
}
