// =====================================================================
// Practica 3 - Contador optico 0-9 (fototransistor + display 7 segmentos)
// VERSION DIGITAL: transistor en CORTE y SATURACION -> gpio_get_level()
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
//
// Portado a ESP-IDF (PlatformIO, framework = espidf) desde el sketch de
// Arduino "contador_optico_v1.ino" de la Practica 2 de Optoelectronica
// (V3736). Misma logica, mismos nombres de macros y mismo circuito.
//
// QUE HACE
//   Un LED emisor ilumina de forma continua a un fototransistor (barrera
//   optica). Cada objeto que CORTA el haz suma +1 a una cuenta 0..9 que
//   se muestra en un display de 7 segmentos de CATODO COMUN (el mismo
//   display y los mismos pines de la Practica 1). Despues del 9 vuelve a 0.
//
// POR QUE "CORTE Y SATURACION"
//   El fototransistor va en emisor comun con R_L (2.2 k) del colector a
//   3V3. La decision 0/1 la hace el HARDWARE:
//     - haz PRESENTE (llega luz)   -> el transistor SATURA -> V_CE ~ 0.2-0.8 V
//                                     -> el GPIO lee 0.
//     - haz INTERRUMPIDO (objeto)  -> el transistor entra en CORTE -> V_CE ~ 3.3 V
//                                     -> el GPIO lee 1.
//   Por eso basta gpio_get_level(): la senal ya viene "cuadrada".
//
// HARDWARE (ESP32-S3-DevKitC-1)  -- todos los pines son SEGUROS
//   Segmentos a..g : GPIO 4, 5, 6, 7, 15, 16, 17  (cada uno -> R 220-330 -> segmento)
//   Comun display  : GND (catodo comun; un 1 ENCIENDE el segmento)
//   Sensor         : GPIO 18 (entrada digital) <- colector del fototransistor
//   Fototransistor : 3V3 -> R_L 2.2 k -> colector ; colector -> GPIO 18 ; emisor -> GND
//   LED emisor     : 5V -> R1 330 -> anodo ; catodo -> GND
//   ** Alimenta el fototransistor a 3.3 V, NO a 5 V: el GPIO no tolera 5 V. **
//   Evitados: 0,3,45,46 (strapping) - 19,20 (USB) - 26..37 (flash/PSRAM)
//
// Uso: copia este archivo en el src/main.c de tu PROYECTO BASE (no crees
//      un proyecto nuevo en clase) y presiona Build / Upload / Monitor.
// =====================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdio.h>

// ---------------- Pines ----------------
static const int SEG[7] = { 4, 5, 6, 7, 15, 16, 17 };  // a,b,c,d,e,f,g
#define PIN_SENSOR   18   // colector del fototransistor (entrada digital)

// ---------------- Criterio de nivel (una sola linea que cambiar) ----------------
// Nivel logico del GPIO cuando el HAZ ESTA LIBRE (sin objeto):
//   0 -> CASO B: se lee el COLECTOR con R_L a 3V3 (cableado de la practica).
//        Con luz el transistor satura -> 0 ; al cortar el haz -> 1.
//        => el objeto se cuenta en el flanco de SUBIDA.
//   1 -> CASO A: se lee el EMISOR (R al emisor / pull-down). Con luz -> 1 ;
//        al cortar el haz -> 0.  => el objeto se cuenta en el flanco de BAJADA.
// (En el sketch de Arduino esta misma macro se escribia LOW / HIGH.)
#define NIVEL_HAZ_LIBRE   0

// El estado "objeto presente" (haz cortado) es el nivel opuesto.
#define NIVEL_OBJETO      (!NIVEL_HAZ_LIBRE)

// Resistencia interna coherente con el nivel de reposo: si el sensor se
// desconecta, el pin NO queda flotando y se lee "haz libre" (no cuenta).
// Nota: la pull interna del ESP32-S3 es debil (~45 k); el nivel real lo
// fija R_L (2.2 k) del circuito. (Arduino: INPUT_PULLUP / INPUT_PULLDOWN.)
#if (NIVEL_HAZ_LIBRE == 1)
  #define MODO_PULL   GPIO_PULLUP_ONLY
#else
  #define MODO_PULL   GPIO_PULLDOWN_ONLY
#endif

// ---------------- Antirrebote (polling) ----------------
// Igual que en el TC2: el estado debe permanecer estable DEBOUNCE_MS
// (= DEBOUNCE_MS / POLL_MS muestras seguidas) para confirmarse.
#define POLL_MS      10   // periodo de muestreo del sensor (tick FreeRTOS = 10 ms)
#define DEBOUNCE_MS  30   // ventana de antirrebote (20-50 ms razonable)

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

// Configura la entrada del fototransistor con la pull interna coherente.
static void init_sensor(void) {
    gpio_reset_pin(PIN_SENSOR);
    gpio_set_direction(PIN_SENSOR, GPIO_MODE_INPUT);
    gpio_set_pull_mode(PIN_SENSOR, MODO_PULL);
}

void app_main(void) {
    init_display();
    init_sensor();

    int cuenta = 0;
    mostrar_digito(cuenta);

    // Variables de antirrebote (patron del TC2). Se inicializan con la
    // lectura REAL del pin para no contar un objeto fantasma al arrancar.
    int lectura_prev    = gpio_get_level(PIN_SENSOR); // ultima lectura cruda
    int lectura_estable = lectura_prev;               // ultimo estado confirmado
    int estable_cnt     = 0;                          // muestras seguidas sin cambio

    printf("Practica 3 - Contador optico 0-9 (MODO DIGITAL: corte/saturacion)\n");
    printf("Sensor en GPIO %d, NIVEL_HAZ_LIBRE = %d, antirrebote = %d ms. Cuenta = 0\n",
           PIN_SENSOR, NIVEL_HAZ_LIBRE, DEBOUNCE_MS);

    while (true) {
        // (a) Lectura cruda del sensor (Arduino: digitalRead)
        int lectura = gpio_get_level(PIN_SENSOR);

        // (b) Antirrebote: contar cuantas muestras seguidas lleva igual
        if (lectura != lectura_prev) {
            estable_cnt = 0;           // cambio: reinicia la cuenta de estabilidad
        } else {
            estable_cnt++;             // mismo valor: acumula estabilidad
        }
        lectura_prev = lectura;

        // (c) Cuando lleva DEBOUNCE_MS estable y es distinto del ultimo
        //     estado confirmado, se acepta el nuevo estado.
        if (estable_cnt >= (DEBOUNCE_MS / POLL_MS) && lectura != lectura_estable) {
            lectura_estable = lectura;

            // (d) Flanco confirmado hacia "objeto presente" = +1 (una sola
            //     vez por objeto; al volver el haz no se cuenta nada).
            if (lectura_estable == NIVEL_OBJETO) {
                cuenta++;
                if (cuenta > 9) cuenta = 0;   // de 9 regresa a 0
                mostrar_digito(cuenta);
                // esp_timer_get_time() da microsegundos desde el arranque
                // (Arduino: millis() == esp_timer_get_time()/1000).
                printf("[%6lld ms] Objeto detectado. Cuenta = %d\n",
                       (long long)(esp_timer_get_time() / 1000), cuenta);
            }
        }

        // (e) Ceder la CPU hasta la siguiente muestra (Arduino: delay)
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
