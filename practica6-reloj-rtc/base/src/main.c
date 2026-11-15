// P6 - Nombre Apellido - codigo - fecha      <-- (cambia esta linea: tu clave de identidad)
// =====================================================================
//  Practica 6 - RELOJ hh:mm con el RTC DS3231 (I2C) en 4 displays (BASE)
//  Seminario de Sistemas Embebidos (I7267) 2026B - Dr. Ruben Estrada
//  ESP32-S3 + ESP-IDF 5.5.0 (PlatformIO, platform = espressif32@6.12.0)
//
//  Que hace: la HORA ya no la cuenta un timer del ESP32-S3 (Practica 4):
//  la lleva el DS3231 con su cristal y su pila. El ESP32-S3 solo la
//  PREGUNTA por I2C y la muestra en los 4 displays multiplexados.
//
//   - tarea_rtc      lee los registros 0x00..0x05 cada 250 ms (I2C, en una
//                    TAREA: transmit_receive bloquea ~0.9 ms) y deja la hora
//                    en variables compartidas. Si falla, marca rtc_ok = false.
//   - refresco_cb    (esp_timer, 2000 us) solo PINTA: nunca toca el I2C.
//   - tarea_consola  comandos del Monitor: poner, hora, osf, temp, ayuda.
//   - app_main       sondea los botones cada 20 ms (antirrebote):
//                    GPIO1 = cambia la vista hh:mm <-> mm:ss
//                    GPIO2 = muestra la fecha dd mm durante 3 s
//
//  Hardware: el MISMO de la Practica 4 (4 digitos de catodo comun, 2N3904,
//  botones en GPIO1/2) + el modulo DS3231 del TC07 (VCC 3V3, SDA GPIO8,
//  SCL GPIO9). Sin RTC o con la hora invalida (OSF = 1) se ven guiones.
//
//  Este es el 80 % que se te entrega FUNCIONANDO. "TODO (tu extra)" marca
//  donde suele entrar el 20 % propio (alarma, 12 h, ajuste con botones,
//  SQW a 1 Hz por interrupcion, temperatura en el display, EEPROM, ...).
//
//  Subobjetivos: [1] salidas/entradas [2] I2C y RTC [3] BCD
//                [4] refrescar [5] botones [6] consola [7] medir
// =====================================================================
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_timer.h"

// ---- [1] Display de la Practica 4 (catodo comun: '1' enciende) ----
static const gpio_num_t SEG[7] = {
    GPIO_NUM_4,  GPIO_NUM_5,  GPIO_NUM_6,  GPIO_NUM_7,   // a b c d
    GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17                // e f g
};
static const gpio_num_t DIG[4] = {                       // D0 D1 D2 D3 (izq. a der.)
    GPIO_NUM_10, GPIO_NUM_11, GPIO_NUM_12, GPIO_NUM_13
};
#define BTN_VISTA    GPIO_NUM_1      // hh:mm <-> mm:ss
#define BTN_FECHA    GPIO_NUM_2      // dd mm durante 3 s

// ---- [2] Bus I2C y RTC ----
#define PIN_SDA      GPIO_NUM_8
#define PIN_SCL      GPIO_NUM_9
#define DS3231_DIR   0x68
#define UART_PC      UART_NUM_0

static const uint8_t PATRON[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};
#define GUION        0x40            // solo el segmento g: "-"

#define N_DIG        4
#define REFRESH_US   2000            // 2 ms por digito, 125 Hz por digito
#define LECTURA_MS   250             // cada cuanto se pregunta la hora al RTC
#define SONDEO_MS    20
#define FECHA_US     3000000

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t rtc;

// ---- Datos compartidos + su candado ----
typedef struct { uint8_t h, min, s, dia, dd, mm, aa; } hora_t;
static hora_t           ahora_rtc;                 // ultima hora leida (decimal)
static volatile bool    rtc_ok      = false;       // false: no responde u OSF = 1
static volatile int     vista       = 0;           // 0 = hh:mm, 1 = mm:ss
static volatile int64_t fecha_hasta = 0;           // mostrar fecha hasta este instante
static volatile uint32_t n_refrescos = 0;
static volatile uint32_t n_lecturas = 0, n_errores = 0;
static volatile int64_t  dur_lectura_us = 0;
static portMUX_TYPE candado = portMUX_INITIALIZER_UNLOCKED;

static const char *DIAS[8] = { "?", "domingo", "lunes", "martes", "miercoles",
                               "jueves", "viernes", "sabado" };

// ---------- Salida al Monitor (una llamada = una linea) ----------
static void imprimir(const char *fmt, ...)
{
    char linea[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(linea, sizeof(linea) - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(linea) - 3) n = sizeof(linea) - 3;
    linea[n++] = '\r';
    linea[n++] = '\n';
    uart_write_bytes(UART_PC, linea, n);
}

// ---------- [3] BCD ----------
static inline uint8_t bcd_a_dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static inline uint8_t dec_a_bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

// ---------- [2] RTC ----------
static void rtc_iniciar(void)
{
    i2c_master_bus_config_t bcfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bcfg, &bus));
    i2c_device_config_t dcfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = DS3231_DIR,
        .scl_speed_hz    = 100000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus, &dcfg, &rtc));
}

static esp_err_t rtc_leer(uint8_t reg, uint8_t *datos, size_t n)
{
    return i2c_master_transmit_receive(rtc, &reg, 1, datos, n, 100);
}

static esp_err_t rtc_poner(const hora_t *t)
{
    uint8_t buf[8] = {
        0x00,                                       // puntero de registro
        dec_a_bcd(t->s), dec_a_bcd(t->min), dec_a_bcd(t->h),   // bit 6 = 0: 24 h
        t->dia, dec_a_bcd(t->dd), dec_a_bcd(t->mm), dec_a_bcd(t->aa),
    };
    esp_err_t r = i2c_master_transmit(rtc, buf, sizeof buf, 100);
    if (r != ESP_OK) return r;
    uint8_t st;                                     // despues de la hora, borra OSF
    r = rtc_leer(0x0F, &st, 1);
    if (r == ESP_OK && (st & 0x80)) {
        uint8_t limpia[2] = { 0x0F, (uint8_t)(st & 0x7F) };
        r = i2c_master_transmit(rtc, limpia, 2, 100);
    }
    return r;
}

static uint8_t dia_semana(int a, int m, int d)      // domingo = 1
{
    static const int k[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) a -= 1;
    return (uint8_t)((a + a / 4 - a / 100 + a / 400 + k[m - 1] + d) % 7 + 1);
}

// ---------- [2][3] Tarea que PREGUNTA la hora (nunca en el callback) ----------
static void tarea_rtc(void *arg)
{
    (void)arg;
    uint8_t ultimo_s = 0xFF;
    bool osf_avisado = false;
    uint32_t prev_ref = n_refrescos;              // [7] para medir refrescos por segundo
    int64_t  prev_t   = esp_timer_get_time();
    for (;;) {
        uint8_t t[7] = { 0 }, st = 0;
        int64_t t0 = esp_timer_get_time();
        esp_err_t r = rtc_leer(0x00, t, sizeof t);
        int64_t dt = esp_timer_get_time() - t0;
        if (r == ESP_OK) r = rtc_leer(0x0F, &st, 1);
        bool valida = (r == ESP_OK) && !(st & 0x80);
        hora_t h = {
            .s   = bcd_a_dec(t[0] & 0x7F), .min = bcd_a_dec(t[1] & 0x7F),
            .h   = bcd_a_dec(t[2] & 0x3F), .dia = t[3] & 0x07,
            .dd  = bcd_a_dec(t[4] & 0x3F), .mm  = bcd_a_dec(t[5] & 0x1F),
            .aa  = bcd_a_dec(t[6]),
        };
        portENTER_CRITICAL(&candado);
        if (r == ESP_OK) ahora_rtc = h;
        rtc_ok = valida;
        n_lecturas++;
        if (r != ESP_OK) n_errores++;
        dur_lectura_us = dt;
        portEXIT_CRITICAL(&candado);

        if (r != ESP_OK) {
            imprimir("[rtc] sin respuesta: %s (revisa VCC, SDA=8, SCL=9)", esp_err_to_name(r));
            ultimo_s = 0xFF;
        } else if (st & 0x80) {
            if (!osf_avisado) imprimir("[rtc] OSF = 1: la hora no es valida. Usa: poner AAAA-MM-DD hh:mm:ss");
            osf_avisado = true;
        } else if (h.s != ultimo_s) {                // una linea por segundo
            osf_avisado = false;
            ultimo_s = h.s;
            int64_t ya = esp_timer_get_time();
            uint32_t ref = n_refrescos;
            uint32_t ref_s = (uint32_t)((ref - prev_ref) * 1000000LL / (ya - prev_t));
            prev_ref = ref;
            prev_t = ya;
            imprimir("%02u:%02u:%02u %s %02u/%02u/20%02u | lectura %lld us | refrescos/s %lu | errores %lu",
                     h.h, h.min, h.s, DIAS[h.dia <= 7 ? h.dia : 0], h.dd, h.mm, h.aa,
                     (long long)dt, (unsigned long)ref_s,
                     (unsigned long)n_errores);
            // TODO (tu extra): comparar h.h:h.min con una alarma y activar un buzzer/LED,
            //   o guardar un registro en la EEPROM 0x57 del modulo.
        }
        vTaskDelay(pdMS_TO_TICKS(LECTURA_MS));
    }
}

// ---------- [4] Refresco: solo pinta lo que ya esta en memoria ----------
static uint8_t dig_activo = 0;

static void escribir_segmentos(uint8_t patron)
{
    for (int i = 0; i < 7; i++) gpio_set_level(SEG[i], (patron >> i) & 0x01);
}

static void refresco_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&candado);
    hora_t h = ahora_rtc;
    bool ok = rtc_ok;
    int v = vista;
    int64_t fh = fecha_hasta;
    portEXIT_CRITICAL(&candado);

    uint8_t izq, der;                              // dos pares de cifras
    if (esp_timer_get_time() < fh) { izq = h.dd;  der = h.mm; }
    else if (v == 0)               { izq = h.h;   der = h.min; }
    else                           { izq = h.min; der = h.s; }
    uint8_t cifra[4] = { izq / 10, izq % 10, der / 10, der % 10 };
    uint8_t patron = ok ? PATRON[cifra[dig_activo] % 10] : GUION;
    // TODO (tu extra): parpadear un digito al ajustar la hora con botones,
    //   mostrar la temperatura, o apagar el display de noche (brillo).

    for (int i = 0; i < N_DIG; i++) gpio_set_level(DIG[i], 0);   // 1) apagar
    escribir_segmentos(patron);                                  // 2) escribir
    gpio_set_level(DIG[dig_activo], 1);                          // 3) encender
    dig_activo = (dig_activo + 1) % N_DIG;                       // 4) avanzar
    n_refrescos++;
}

// ---------- [6] Consola del Monitor ----------
static void cmd_poner(const char *arg)
{
    int A, M, D, hh, mi, ss;
    if (sscanf(arg, "%d-%d-%d %d:%d:%d", &A, &M, &D, &hh, &mi, &ss) != 6 ||
        A < 2000 || A > 2099 || M < 1 || M > 12 || D < 1 || D > 31 ||
        hh < 0 || hh > 23 || mi < 0 || mi > 59 || ss < 0 || ss > 59) {
        imprimir("uso: poner AAAA-MM-DD hh:mm:ss   (ejemplo: poner 2026-11-19 13:05:00)");
        return;
    }
    hora_t t = { .h = hh, .min = mi, .s = ss, .dia = dia_semana(A, M, D),
                 .dd = D, .mm = M, .aa = (uint8_t)(A - 2000) };
    esp_err_t r = rtc_poner(&t);
    imprimir(r == ESP_OK ? "hora puesta (%s) y OSF borrado" : "no se pudo: %s",
             r == ESP_OK ? DIAS[t.dia] : esp_err_to_name(r));
}

static void cmd_temp(void)
{
    uint8_t t[2];
    esp_err_t r = rtc_leer(0x11, t, 2);
    if (r != ESP_OK) { imprimir("[error] %s", esp_err_to_name(r)); return; }
    imprimir("T = %.2f C", (int8_t)t[0] + (t[1] >> 6) * 0.25f);
}

static void procesar(char *l)
{
    while (*l == ' ') l++;
    for (char *p = l; *p && *p != ' '; p++) *p = (char)tolower((unsigned char)*p);
    char *arg = strchr(l, ' ');
    if (arg) { *arg++ = '\0'; while (*arg == ' ') arg++; } else arg = "";
    if (*l == '\0') return;
    if (!strcmp(l, "poner")) cmd_poner(arg);
    else if (!strcmp(l, "temp")) cmd_temp();
    else if (!strcmp(l, "osf")) {
        uint8_t st; esp_err_t r = rtc_leer(0x0F, &st, 1);
        if (r == ESP_OK) imprimir("0x0F = %02X  OSF = %u", st, st >> 7);
        else imprimir("[error] %s", esp_err_to_name(r));
    }
    else if (!strcmp(l, "ayuda"))
        imprimir("Comandos: poner AAAA-MM-DD hh:mm:ss | temp | osf | ayuda. Botones: GPIO1 vista, GPIO2 fecha");
    // TODO (tu extra): nuevos comandos, p. ej. "alarma 07:30" o "formato 12"
    else imprimir("no conozco '%s'. Escribe ayuda", l);
}

static void tarea_consola(void *arg)
{
    (void)arg;
    static char linea[81];
    size_t largo = 0;
    uint8_t buf[32];
    for (;;) {
        int n = uart_read_bytes(UART_PC, buf, sizeof buf, pdMS_TO_TICKS(50));
        for (int i = 0; i < n; ++i) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') { linea[largo] = '\0'; procesar(linea); largo = 0; }
            else if ((c == 0x08 || c == 0x7F) && largo) --largo;
            else if (largo < sizeof linea - 1 && c >= ' ') linea[largo++] = c;
        }
    }
}

// ---------- [1] GPIO ----------
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
    const uart_config_t ucfg = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PC, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC, &ucfg));
    uart_vfs_dev_use_driver(UART_PC);

    for (int i = 0; i < 7; i++)     config_salida(SEG[i]);          // [1]
    for (int i = 0; i < N_DIG; i++) config_salida(DIG[i]);
    config_entrada_pullup(BTN_VISTA);
    config_entrada_pullup(BTN_FECHA);
    rtc_iniciar();                                                  // [2]

    const esp_timer_create_args_t a_ref = { .callback = &refresco_cb, .name = "refresco" };
    esp_timer_handle_t t_refresco;
    ESP_ERROR_CHECK(esp_timer_create(&a_ref, &t_refresco));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t_refresco, REFRESH_US));   // [4]

    imprimir("");
    imprimir("Practica 6 - Reloj hh:mm con DS3231 (0x68, SDA=8, SCL=9). Refresco %d us, lectura cada %d ms",
             REFRESH_US, LECTURA_MS);
    imprimir("Comandos: poner AAAA-MM-DD hh:mm:ss | temp | osf | ayuda. Botones: GPIO1 vista, GPIO2 fecha");
    xTaskCreate(tarea_rtc, "rtc", 4096, NULL, 5, NULL);                // [2]
    xTaskCreate(tarea_consola, "consola", 4096, NULL, 4, NULL);        // [6]

    // ---- [5] Botones por sondeo (activos en bajo) ----
    int prev_v = 1, prev_f = 1;
    while (1) {
        int v = gpio_get_level(BTN_VISTA);
        int f = gpio_get_level(BTN_FECHA);
        if (v == 0 && prev_v == 1) {
            portENTER_CRITICAL(&candado);
            vista = !vista;
            int nv = vista;
            portEXIT_CRITICAL(&candado);
            imprimir("vista: %s", nv ? "mm:ss" : "hh:mm");
        }
        if (f == 0 && prev_f == 1) {
            portENTER_CRITICAL(&candado);
            fecha_hasta = esp_timer_get_time() + FECHA_US;
            portEXIT_CRITICAL(&candado);
            imprimir("fecha (dd mm) durante 3 s");
        }
        // TODO (tu extra): pulsacion larga para entrar a modo ajuste y subir horas/minutos
        prev_v = v;
        prev_f = f;
        vTaskDelay(pdMS_TO_TICKS(SONDEO_MS));
    }
}
