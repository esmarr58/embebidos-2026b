// TC06 - Nombre Apellido - codigo - fecha
// Chat por UART2 entre dos ESP32-S3. ESP-IDF 5.5.0 / espressif32@6.12.0.
// El MISMO firmware va en las dos tarjetas. Lo que tecleas en el Monitor de
// PlatformIO (UART0, USB) sale por UART2 hacia la otra tarjeta; lo que llega
// por UART2 se imprime en tu Monitor con el nombre del otro.
//
//   UART0  (GPIO43 TX / GPIO44 RX, van al CP2102 del DevKitC-1) <-> tu laptop
//   UART2  (GPIO17 TX / GPIO18 RX)                             <-> otra tarjeta
//   Cruzado: GPIO17 (TX) de una -> GPIO18 (RX) de la otra, y viceversa. GND comun.
//   Loopback (una sola tarjeta): puentea GPIO17 con GPIO18; lo que mandas vuelve.
//
// Comandos (se escriben en el Monitor, empiezan con '/'):
//   /nombre Ana   apodo que vera el otro      /ping        prueba el enlace
//   /baud 9600    cambia el baud de UART2     /ayuda       lista de comandos
//
// Trama en el cable (texto, una linea terminada en '\n'):
//   nombre:texto      mensaje de chat
//   @hola nombre      me presento y pido que el otro se presente (@soy)
//   @soy nombre       respuesta a @hola, o aviso de cambio de nombre
//   @ping id          pide respuesta;  @pong id   la respuesta
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_err.h"

#define UART_PC        UART_NUM_0   // consola USB: la misma que usa el Monitor
#define UART_CHAT      UART_NUM_2   // enlace con la otra tarjeta
#define PIN_TX         GPIO_NUM_17  // TX de UART2 -> RX (GPIO18) de la otra
#define PIN_RX         GPIO_NUM_18  // RX de UART2 <- TX (GPIO17) de la otra
#define BAUD_PC        115200       // igual que monitor_speed en platformio.ini
#define BAUD_CHAT      115200       // 8N1. Prueba 3 de la guia: 9600 en UNA tarjeta
#define BUF_RX         1024         // bytes del anillo de recepcion de cada UART
#define LINEA_MAX      200          // largo maximo de una linea de chat
#define PING_TIMEOUT_MS 2000        // sin @pong en este tiempo => sin respuesta
#define NOMBRE_MAX     20

static char mi_nombre[NOMBRE_MAX + 1];           // "S3-xxxx" por defecto (MAC)
static char otro_nombre[NOMBRE_MAX + 1] = "Otro"; // se actualiza con @hola/@soy
static QueueHandle_t cola_uart2;                  // eventos del driver de UART2
static volatile bool esperando_pong = false;
static int64_t t_ping_us;

// ---------- Salida al Monitor -------------------------------------------
// Una vez instalado el driver en UART0 escribimos SIEMPRE con uart_write_bytes:
// el driver encola los bytes y los saca por interrupcion, sin mezclarse con
// otro emisor. uart_vfs_dev_use_driver() redirige ademas printf/ESP_LOG por
// el mismo driver, asi que no habria choque; aqui usamos un unico camino para
// que el alumno vea una sola forma de escribir. Cada llamada = una linea.
static void imprimir(const char *fmt, ...)
{
    char linea[LINEA_MAX + 64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(linea, sizeof(linea) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(linea) - 3) n = sizeof(linea) - 3;
    linea[n++] = '\r';           // el Monitor de PlatformIO espera CR+LF
    linea[n++] = '\n';
    uart_write_bytes(UART_PC, linea, n);
}

// ---------- Salida hacia la otra tarjeta ----------------------------------
static void enviar_trama(const char *fmt, ...)
{
    char trama[LINEA_MAX + 64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(trama, sizeof(trama) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(trama) - 2) n = sizeof(trama) - 2;
    trama[n++] = '\n';           // fin de trama: una linea = un mensaje
    uart_write_bytes(UART_CHAT, trama, n);
}

// Deja solo caracteres imprimibles: la basura por baud distinto no debe
// desordenar el terminal. Los bytes >= 0x80 se muestran como '?'.
static void limpiar_texto(char *s)
{
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c < 0x20 || c >= 0x7F) *s = '?';
    }
}

static void mostrar_ayuda(void)
{
    imprimir("[sistema] Comandos: /nombre <apodo>  /ping  /baud <bps>  /ayuda");
    imprimir("[sistema] Todo lo demas se envia como mensaje. Enter = enviar.");
}

// ---------- Lo que teclea el alumno (viene por UART0) ----------------------
static void procesar_linea_local(char *linea)
{
    if (linea[0] == '\0') return;

    if (linea[0] != '/') {                          // mensaje normal
        enviar_trama("%s:%s", mi_nombre, linea);
        imprimir("[Yo] %s", linea);                  // eco local con prefijo
        return;
    }
    if (strncmp(linea, "/nombre ", 8) == 0 && linea[8] != '\0') {
        snprintf(mi_nombre, sizeof(mi_nombre), "%s", linea + 8);
        enviar_trama("@soy %s", mi_nombre);
        imprimir("[sistema] ahora te llamas %s", mi_nombre);
    } else if (strcmp(linea, "/ping") == 0) {
        esperando_pong = true;
        t_ping_us = esp_timer_get_time();
        enviar_trama("@ping %s", mi_nombre);
        imprimir("[sistema] ping enviado por GPIO17, esperando @pong por GPIO18...");
    } else if (strncmp(linea, "/baud ", 6) == 0) {
        uint32_t baud = (uint32_t)strtoul(linea + 6, NULL, 10);
        if (baud < 300 || baud > 921600) {
            imprimir("[sistema] baud fuera de rango (300..921600)");
            return;
        }
        ESP_ERROR_CHECK(uart_set_baudrate(UART_CHAT, baud));
        imprimir("[sistema] UART2 ahora a %" PRIu32 " bps 8N1 (tiempo de bit = %.1f us)",
                 baud, 1e6 / (double)baud);
    } else if (strcmp(linea, "/ayuda") == 0) {
        mostrar_ayuda();
    } else {
        imprimir("[sistema] comando desconocido: %s", linea);
        mostrar_ayuda();
    }
}

// Tarea 1: lee UART0 en bloques (timeout 20 ms), junta una linea hasta Enter
// y la procesa. Tambien vigila el tiempo de espera del /ping.
static void tarea_teclado(void *arg)
{
    (void)arg;
    static char linea[LINEA_MAX + 1];
    size_t largo = 0;
    uint8_t buf[64];
    for (;;) {
        int n = uart_read_bytes(UART_PC, buf, sizeof(buf), pdMS_TO_TICKS(20));
        for (int i = 0; i < n; ++i) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {            // Enter: LF, CR o CRLF
                linea[largo] = '\0';
                procesar_linea_local(linea);
                largo = 0;
            } else if (c == 0x08 || c == 0x7F) {     // retroceso
                if (largo) --largo;
            } else if (largo < LINEA_MAX) {
                linea[largo++] = c;
            }
        }
        if (esperando_pong && esp_timer_get_time() - t_ping_us > PING_TIMEOUT_MS * 1000LL) {
            esperando_pong = false;
            imprimir("[sistema] sin respuesta: revisa TX<->RX cruzados (17->18, 18<-17) y GND comun");
        }
    }
}

// ---------- Lo que llega de la otra tarjeta (viene por UART2) ---------------
static void procesar_trama(char *trama)
{
    if (trama[0] == '\0') return;
    limpiar_texto(trama);

    if (trama[0] == '@') {                          // tramas de control
        char *arg = strchr(trama, ' ');
        if (arg) *arg++ = '\0'; else arg = "";
        if (strcmp(trama, "@ping") == 0) {
            if (strcmp(arg, mi_nombre) == 0) {       // volvio mi propio ping
                esperando_pong = false;
                imprimir("[sistema] LOOPBACK: tu ping volvio por GPIO18 en %" PRId64 " us. "
                         "UART2 funciona; GPIO17 y GPIO18 estan puenteados.",
                         esp_timer_get_time() - t_ping_us);
            } else {
                imprimir("[sistema] ping de %s: respondo @pong", arg);
                enviar_trama("@pong %s", mi_nombre);
            }
        } else if (strcmp(trama, "@pong") == 0) {
            if (esperando_pong) {
                esperando_pong = false;
                imprimir("[sistema] %s respondio en %" PRId64 " us: enlace UART2 OK",
                         arg, esp_timer_get_time() - t_ping_us);
            }
        } else if (strcmp(trama, "@hola") == 0 || strcmp(trama, "@soy") == 0) {
            if (strcmp(arg, mi_nombre) == 0) {
                imprimir("[sistema] LOOPBACK: recibo mi propia presentacion (17 y 18 puenteados)");
                return;
            }
            if (arg[0]) snprintf(otro_nombre, sizeof(otro_nombre), "%s", arg);
            imprimir("[sistema] la otra tarjeta se llama %s", otro_nombre);
            if (strcmp(trama, "@hola") == 0) enviar_trama("@soy %s", mi_nombre);
        } else {
            imprimir("[?] %s %s", trama, arg);
        }
        return;
    }

    char *sep = strchr(trama, ':');                 // mensaje nombre:texto
    if (sep) {
        *sep = '\0';
        if (strcmp(trama, mi_nombre) == 0) {
            imprimir("[Eco] %s   <- loopback: es tu propio mensaje", sep + 1);
        } else {
            if (strcmp(trama, otro_nombre) != 0) snprintf(otro_nombre, sizeof(otro_nombre), "%s", trama);
            imprimir("[%s] %s", trama, sep + 1);
        }
    } else {
        // Sin formato: basura por baud distinto, o un terminal ajeno conectado.
        imprimir("[?] %s   <- trama sin formato: baud distinto o ruido", trama);
    }
}

// Tarea 2: espera eventos del driver de UART2. Junta bytes hasta '\n' y
// reporta los errores de linea que el hardware detecta (framing, overflow).
static void tarea_uart2(void *arg)
{
    (void)arg;
    static char linea[LINEA_MAX + 1];
    size_t largo = 0;
    uint8_t buf[128];
    uart_event_t ev;
    for (;;) {
        if (xQueueReceive(cola_uart2, &ev, portMAX_DELAY) != pdTRUE) continue;
        switch (ev.type) {
        case UART_DATA: {
            size_t pendientes = ev.size;
            while (pendientes) {
                size_t pedir = pendientes > sizeof(buf) ? sizeof(buf) : pendientes;
                int n = uart_read_bytes(UART_CHAT, buf, pedir, pdMS_TO_TICKS(20));
                if (n <= 0) break;
                pendientes -= (size_t)n;
                for (int i = 0; i < n; ++i) {
                    char c = (char)buf[i];
                    if (c == '\n') {
                        linea[largo] = '\0';
                        procesar_trama(linea);
                        largo = 0;
                    } else if (c != '\r' && largo < LINEA_MAX) {
                        linea[largo++] = c;
                    } else if (largo >= LINEA_MAX) {   // nunca llego '\n'
                        linea[largo] = '\0';
                        imprimir("[sistema] linea demasiado larga sin fin de linea: baud distinto?");
                        procesar_trama(linea);
                        largo = 0;
                    }
                }
            }
            break;
        }
        case UART_FRAME_ERR:
            imprimir("[sistema] FRAME ERROR en UART2: bit de paro no valido. "
                     "Mismo baud (8N1) en las dos tarjetas? GND comun?");
            break;
        case UART_PARITY_ERR:
            imprimir("[sistema] PARITY ERROR en UART2 (no usamos paridad: ruido o baud distinto)");
            break;
        case UART_BREAK:
            imprimir("[sistema] BREAK en UART2: linea RX en bajo mas de una trama (cable suelto o reset del otro)");
            break;
        case UART_FIFO_OVF:
        case UART_BUFFER_FULL:
            imprimir("[sistema] desborde de recepcion en UART2: se descartan %u bytes", (unsigned)ev.size);
            uart_flush_input(UART_CHAT);
            xQueueReset(cola_uart2);
            largo = 0;
            break;
        default:
            break;
        }
    }
}

// ---------- Configuracion -------------------------------------------------
static void configurar_uarts(void)
{
    const uart_config_t cfg_pc = {
        .baud_rate = BAUD_PC, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // UART0 conserva sus pines de fabrica (GPIO43/44 -> CP2102): no se tocan.
    ESP_ERROR_CHECK(uart_driver_install(UART_PC, BUF_RX, BUF_RX, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC, &cfg_pc));
    // A partir de aqui printf/ESP_LOG tambien salen por el driver (sin choque).
    uart_vfs_dev_use_driver(UART_PC);

    const uart_config_t cfg_chat = {
        .baud_rate = BAUD_CHAT, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // 20 eventos en la cola: UART_DATA, FRAME_ERR, FIFO_OVF...
    ESP_ERROR_CHECK(uart_driver_install(UART_CHAT, BUF_RX, BUF_RX, 20, &cola_uart2, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_CHAT, &cfg_chat));
    ESP_ERROR_CHECK(uart_set_pin(UART_CHAT, PIN_TX, PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void nombre_por_defecto(void)
{
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);        // unico por chip: "S3-" + 2 bytes
    snprintf(mi_nombre, sizeof(mi_nombre), "S3-%02X%02X", mac[4], mac[5]);
}

void app_main(void)
{
    nombre_por_defecto();
    configurar_uarts();
    imprimir("");
    imprimir("TC06 | CHAT UART2 | ARRANQUE | soy %s", mi_nombre);
    imprimir("UART0 USB %d bps <-> Monitor | UART2 %d bps 8N1: TX=GPIO%d RX=GPIO%d | GND comun",
             BAUD_PC, BAUD_CHAT, PIN_TX, PIN_RX);
    mostrar_ayuda();
    imprimir("[sistema] escribe /ping para comprobar el cable antes de chatear");

    xTaskCreate(tarea_uart2, "uart2_rx", 4096, NULL, 10, NULL);
    xTaskCreate(tarea_teclado, "uart0_rx", 4096, NULL, 9, NULL);

    enviar_trama("@hola %s", mi_nombre);   // si el otro ya esta encendido, se presenta
}
