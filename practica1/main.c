// =====================================================================
// Practica 1 - Contador en display de 7 segmentos (catodo comun)
// Seminario de Sistemas Embebidos (I7267) - Dr. Ruben Estrada, CUCEI-UDG
//
// CODIGO BASE: muestra 0..9 en un display de 7 segmentos de CATODO COMUN
//              (el pin comun va a GND; un segmento enciende con nivel 1).
//
// TU TAREA:  agrega un BOTON (pull-down) que cambie el SENTIDO de la
//            cuenta (ascendente <-> descendente). Busca los "TODO".
//
// Uso: copia este archivo en el src/main.c de tu PROYECTO BASE (no crees
//      un proyecto nuevo en clase) y presiona Build/Upload.
// =====================================================================
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

// --- Segmentos a..g en pines SEGUROS del ESP32-S3 --------------------
//     Evita:  0,3,45,46 (strapping) · 26-37 (flash/PSRAM) · 19,20 (USB)
static const int SEG[7] = { 4, 5, 6, 7, 15, 16, 17 };  // a,b,c,d,e,f,g

#define BOTON 18   // boton con pull-down externo (reposo = 0, presionado = 1)

// --- Decodificador BCD -> 7 segmentos --------------------------------
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

// Configura los 7 pines de segmento como salida.
static void init_display(void) {
    for (int i = 0; i < 7; i++) {
        gpio_reset_pin(SEG[i]);
        gpio_set_direction(SEG[i], GPIO_MODE_OUTPUT);
    }
}

// Muestra un digito (0..9): escribe cada segmento segun su bit.
static void mostrar_digito(uint8_t n) {
    if (n > 9) return;
    uint8_t m = DIGITO[n];
    for (int i = 0; i < 7; i++)
        gpio_set_level(SEG[i], (m >> i) & 1);   // bit i -> segmento i
}

void app_main(void) {
    init_display();

    // TODO (1): configura el boton como ENTRADA con pull-down.
    //   gpio_reset_pin(BOTON);
    //   gpio_set_direction(BOTON, GPIO_MODE_INPUT);
    //   gpio_set_pull_mode(BOTON, GPIO_PULLDOWN_ONLY);

    int cuenta  = 0;
    int sentido = +1;   // +1 = ascendente, -1 = descendente

    while (true) {
        mostrar_digito(cuenta);
        vTaskDelay(700 / portTICK_PERIOD_MS);   // ~0.7 s por numero

        // TODO (2): si el boton se presiono (flanco 0->1, con antirrebote),
        //           invierte el sentido:   sentido = -sentido;

        cuenta += sentido;
        if (cuenta > 9) cuenta = 0;    // vuelve a 0 tras el 9
        if (cuenta < 0) cuenta = 9;    // vuelve a 9 tras el 0
    }
}
