// TC07 - Nombre Apellido - codigo - fecha
// El RTC DS3231 por I2C, manejado desde el Monitor de PlatformIO.
// ESP-IDF 5.5.0 / espressif32@6.12.0, driver nuevo i2c_master.h (sin Arduino).
//
//   ESP32-S3-DevKitC-1          Modulo DS3231 (ZS-042)
//   3V3  ---------------------- VCC   (3.3 V: asi SDA/SCL quedan a 3.3 V)
//   GND  ---------------------- GND
//   GPIO8 --------------------- SDA   (pull-up de 4.7 k ya viene en el modulo)
//   GPIO9 --------------------- SCL
//
// Comandos (escribelos en el Monitor y pulsa Enter):
//   scan                       busca chips en 0x08..0x77 (DS3231 = 0x68, EEPROM = 0x57)
//   hora                       lee 0x00..0x06: bytes BCD crudos y la hora decodificada
//   auto                       enciende/apaga la hora cada segundo
//   poner AAAA-MM-DD hh:mm:ss  pone fecha y hora en UNA escritura y borra OSF
//   temp                       temperatura interna (registros 0x11 y 0x12)
//   osf                        lee el registro de estado 0x0F y explica el bit OSF
//   regs                       vuelca los registros 0x00..0x12 en hexadecimal
//   dir 0x68                   cambia la direccion a la que se habla (prueba NACK)
//   vel 100 | vel 400          reloj SCL en kHz (modo estandar o rapido)
//   reset                      libera el bus (pulsos en SCL) si un esclavo quedo pegado
//   ayuda                      esta lista
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_timer.h"
#include "esp_err.h"

#define PIN_SDA      GPIO_NUM_8          // libres junto al display de la P4
#define PIN_SCL      GPIO_NUM_9
#define DS3231_DIR   0x68                // 7 bits, sin el bit R/W
#define EEPROM_DIR   0x57                // AT24C32 del mismo modulo
#define UART_PC      UART_NUM_0          // consola USB (la del Monitor)
#define LINEA_MAX    80
#define TIMEOUT_MS   100

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t rtc;
static uint16_t dir_rtc = DS3231_DIR;
static uint32_t vel_hz  = 100000;        // 100 kHz, modo estandar
static bool     modo_auto = false;

static const char *DIAS[8] = { "?", "domingo", "lunes", "martes", "miercoles",
                               "jueves", "viernes", "sabado" };

// ---------- Salida al Monitor (una llamada = una linea, CR+LF) ----------
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

static inline uint8_t bcd_a_dec(uint8_t b) { return (uint8_t)((b >> 4) * 10 + (b & 0x0F)); }
static inline uint8_t dec_a_bcd(uint8_t d) { return (uint8_t)(((d / 10) << 4) | (d % 10)); }

// ---------- I2C: el bus una vez, el dispositivo (re)configurable ----------
static void bus_iniciar(void)
{
    i2c_master_bus_config_t bcfg = {                 // [1] el bus: puerto, pines y reloj
        .i2c_port = I2C_NUM_0, .sda_io_num = PIN_SDA, .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,        // ~45 k: ayuda, no sustituye a las 4.7 k
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bcfg, &bus));
}

static esp_err_t rtc_agregar(void)
{
    if (rtc) {                                       // si ya existia, se quita primero
        i2c_master_bus_rm_device(rtc);
        rtc = NULL;
    }
    i2c_device_config_t dcfg = {                     // [2] el dispositivo en ese bus
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = dir_rtc,
        .scl_speed_hz    = vel_hz,
    };
    return i2c_master_bus_add_device(bus, &dcfg, &rtc);
}

// [5] escribe el puntero de registro y, con START repetido, lee n bytes
static esp_err_t rtc_leer(uint8_t reg, uint8_t *datos, size_t n)
{
    return i2c_master_transmit_receive(rtc, &reg, 1, datos, n, TIMEOUT_MS);
}

static void explicar_error(esp_err_t r)
{
    imprimir("[error] %s", esp_err_to_name(r));
    if (r == ESP_ERR_INVALID_STATE)
        imprimir("        NACK (nadie con la direccion 0x%02X: cable SDA/SCL, VCC o direccion) o bus atascado.", dir_rtc);
    else if (r == ESP_ERR_TIMEOUT)
        imprimir("        SCL o SDA no suben: sin pull-ups, corto a GND o esclavo pegado. Prueba: reset");
}

// ---------- Comandos ----------
static void cmd_scan(void)
{
    imprimir("Escaneando 0x08..0x77 (SDA=GPIO8, SCL=GPIO9, probe a 100 kHz)");
    int encontrados = 0;
    for (uint16_t d = 0x08; d <= 0x77; d++) {
        esp_err_t r = i2c_master_probe(bus, d, 50);  // [3] START + dir + W: ACK?
        if (r == ESP_OK) {
            const char *que = (d == DS3231_DIR) ? "DS3231 (RTC)" :
                              (d == EEPROM_DIR) ? "AT24C32 (EEPROM del modulo)" : "desconocido";
            imprimir("  0x%02X responde (ACK)  <- %s", d, que);
            encontrados++;
        } else if (r == ESP_ERR_TIMEOUT) {
            imprimir("  bus atascado en 0x%02X: revisa pull-ups, VCC y cables", d);
            break;
        }                                            // ESP_ERR_NOT_FOUND = NACK: nadie
    }
    imprimir("%d dispositivo(s) en el bus", encontrados);
}

static bool cmd_hora(bool crudo)
{
    uint8_t t[7];                                    // s, min, h, dia, fecha, mes, anio
    int64_t t0 = esp_timer_get_time();
    esp_err_t r = rtc_leer(0x00, t, sizeof t);
    int64_t dt = esp_timer_get_time() - t0;
    if (r != ESP_OK) { explicar_error(r); return false; }
    uint8_t s   = bcd_a_dec(t[0] & 0x7F);
    uint8_t min = bcd_a_dec(t[1] & 0x7F);
    uint8_t h   = bcd_a_dec(t[2] & 0x3F);            // modo 24 h: bit 6 = 0
    uint8_t ds  = t[3] & 0x07;
    uint8_t dd  = bcd_a_dec(t[4] & 0x3F);
    uint8_t mm  = bcd_a_dec(t[5] & 0x1F);            // bit 7 = siglo
    uint8_t aa  = bcd_a_dec(t[6]);
    if (crudo)
        imprimir("crudo 0x00..0x06: %02X %02X %02X %02X %02X %02X %02X   (lectura en %lld us a %lu kHz)",
                 t[0], t[1], t[2], t[3], t[4], t[5], t[6], (long long)dt, (unsigned long)(vel_hz / 1000));
    imprimir("%02u:%02u:%02u  %s %02u/%02u/20%02u", h, min, s, DIAS[ds <= 7 ? ds : 0], dd, mm, aa);
    return true;
}

// dia de la semana con domingo = 1 (convencion de este curso para el registro 0x03)
static uint8_t dia_semana(int a, int m, int d)
{
    static const int k[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };   // Sakamoto
    if (m < 3) a -= 1;
    int w = (a + a / 4 - a / 100 + a / 400 + k[m - 1] + d) % 7;          // 0 = domingo
    return (uint8_t)(w + 1);
}

static void cmd_poner(const char *arg)
{
    int A, M, D, h, mi, s;
    if (sscanf(arg, "%d-%d-%d %d:%d:%d", &A, &M, &D, &h, &mi, &s) != 6 ||
        A < 2000 || A > 2099 || M < 1 || M > 12 || D < 1 || D > 31 ||
        h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59) {
        imprimir("uso: poner AAAA-MM-DD hh:mm:ss   (ejemplo: poner 2026-11-17 13:45:00)");
        return;
    }
    uint8_t ds = dia_semana(A, M, D);
    uint8_t buf[8] = {                               // [4] puntero 0x00 + 7 datos, UNA transaccion
        0x00,
        dec_a_bcd((uint8_t)s), dec_a_bcd((uint8_t)mi),
        dec_a_bcd((uint8_t)h),                       // bit 6 = 0: modo 24 h
        ds, dec_a_bcd((uint8_t)D), dec_a_bcd((uint8_t)M), dec_a_bcd((uint8_t)(A - 2000)),
    };
    esp_err_t r = i2c_master_transmit(rtc, buf, sizeof buf, TIMEOUT_MS);
    if (r != ESP_OK) { explicar_error(r); return; }
    imprimir("escrito: %02X %02X %02X %02X %02X %02X %02X %02X  (puntero + 7 bytes BCD; dia %u = %s)",
             buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], ds, DIAS[ds]);
    uint8_t st;                                      // despues de la hora se borra OSF
    if (rtc_leer(0x0F, &st, 1) == ESP_OK && (st & 0x80)) {
        uint8_t limpia[2] = { 0x0F, (uint8_t)(st & 0x7F) };
        if (i2c_master_transmit(rtc, limpia, 2, TIMEOUT_MS) == ESP_OK)
            imprimir("OSF estaba en 1: borrado (la hora ya es valida)");
    }
    cmd_hora(false);
}

static void cmd_temp(void)
{
    uint8_t t[2];
    esp_err_t r = rtc_leer(0x11, t, 2);
    if (r != ESP_OK) { explicar_error(r); return; }
    float c = (int8_t)t[0] + (t[1] >> 6) * 0.25f;   // 0x11 con signo, bits 7:6 de 0x12
    imprimir("0x11 = %02X  0x12 = %02X  ->  T = %.2f C  (el DS3231 la actualiza cada 64 s)", t[0], t[1], c);
}

static void cmd_osf(void)
{
    uint8_t st;
    esp_err_t r = rtc_leer(0x0F, &st, 1);
    if (r != ESP_OK) { explicar_error(r); return; }
    imprimir("0x0F = %02X  OSF (bit 7) = %u", st, (st >> 7) & 1);
    imprimir(st & 0x80 ? "OSF = 1: el oscilador se detuvo (sin VCC ni pila). La hora NO es de fiar: usa poner."
                       : "OSF = 0: el oscilador no se ha detenido desde que se puso la hora.");
}

static void cmd_regs(void)
{
    uint8_t r[0x13];
    esp_err_t e = rtc_leer(0x00, r, sizeof r);
    if (e != ESP_OK) { explicar_error(e); return; }
    imprimir("     +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F");
    imprimir("0x00 %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
    imprimir("0x10 %02X %02X %02X", r[16], r[17], r[18]);
}

static void cmd_dir(const char *arg)
{
    unsigned long d = strtoul(arg, NULL, 0);
    if (d < 0x08 || d > 0x77) { imprimir("uso: dir 0x68   (7 bits, de 0x08 a 0x77)"); return; }
    dir_rtc = (uint16_t)d;
    esp_err_t r = rtc_agregar();
    if (r != ESP_OK) { explicar_error(r); return; }
    imprimir("ahora se habla a 0x%02X (byte de escritura 0x%02X, de lectura 0x%02X). Prueba: hora",
             dir_rtc, dir_rtc << 1, (dir_rtc << 1) | 1);
}

static void cmd_vel(const char *arg)
{
    int k = atoi(arg);
    if (k != 100 && k != 400) { imprimir("uso: vel 100  o  vel 400   (kHz)"); return; }
    vel_hz = (uint32_t)k * 1000;
    esp_err_t r = rtc_agregar();
    if (r != ESP_OK) { explicar_error(r); return; }
    imprimir("SCL a %d kHz: un bit dura %.2f us. Prueba: hora (mira el tiempo de lectura)", k, 1000.0f / k);
}

static void cmd_ayuda(void)
{
    imprimir("Comandos: scan | hora | auto | poner AAAA-MM-DD hh:mm:ss | temp | osf | regs");
    imprimir("          dir 0x68 | vel 100 | vel 400 | reset | ayuda");
}

static void procesar(char *l)
{
    while (*l == ' ') l++;
    for (char *p = l; *p && *p != ' '; p++) *p = (char)tolower((unsigned char)*p);
    char *arg = strchr(l, ' ');
    if (arg) { *arg++ = '\0'; while (*arg == ' ') arg++; } else arg = "";
    if (*l == '\0') return;
    imprimir("> %s %s", l, arg);
    if      (!strcmp(l, "scan"))  cmd_scan();
    else if (!strcmp(l, "hora"))  cmd_hora(true);
    else if (!strcmp(l, "auto"))  { modo_auto = !modo_auto; imprimir("hora cada segundo: %s", modo_auto ? "SI" : "NO"); }
    else if (!strcmp(l, "poner")) cmd_poner(arg);
    else if (!strcmp(l, "temp"))  cmd_temp();
    else if (!strcmp(l, "osf"))   cmd_osf();
    else if (!strcmp(l, "regs"))  cmd_regs();
    else if (!strcmp(l, "dir"))   cmd_dir(arg);
    else if (!strcmp(l, "vel"))   cmd_vel(arg);
    else if (!strcmp(l, "reset")) {
        esp_err_t r = i2c_master_bus_reset(bus);     // hasta 9 pulsos en SCL (hardware)
        imprimir("reset del bus: %s", esp_err_to_name(r));
    }
    else if (!strcmp(l, "ayuda")) cmd_ayuda();
    else imprimir("no conozco '%s'. Escribe ayuda", l);
}

// ---------- Tarea de consola: lee lineas del Monitor (UART0) ----------
static void tarea_consola(void *arg)
{
    (void)arg;
    static char linea[LINEA_MAX + 1];
    size_t largo = 0;
    uint8_t buf[32];
    int64_t prox_auto = 0;
    for (;;) {
        int n = uart_read_bytes(UART_PC, buf, sizeof buf, pdMS_TO_TICKS(20));
        for (int i = 0; i < n; ++i) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                linea[largo] = '\0';
                procesar(linea);
                largo = 0;
            } else if (c == 0x08 || c == 0x7F) {
                if (largo) --largo;
            } else if (largo < LINEA_MAX) {
                linea[largo++] = c;
            }
        }
        if (modo_auto && esp_timer_get_time() >= prox_auto) {   // leer I2C en una TAREA, nunca en una ISR
            prox_auto = esp_timer_get_time() + 1000000;
            cmd_hora(false);
        }
    }
}

void app_main(void)
{
    const uart_config_t cfg = {
        .baud_rate = 115200, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_PC, 1024, 1024, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PC, &cfg));
    uart_vfs_dev_use_driver(UART_PC);                // los logs del driver I2C salen por aqui

    bus_iniciar();
    ESP_ERROR_CHECK(rtc_agregar());
    imprimir("");
    imprimir("TC07 | RTC DS3231 POR I2C | SDA=GPIO8 SCL=GPIO9 | 0x%02X a %lu kHz", dir_rtc,
             (unsigned long)(vel_hz / 1000));
    cmd_ayuda();
    imprimir("Empieza con: scan");
    xTaskCreate(tarea_consola, "consola", 4096, NULL, 5, NULL);
}
