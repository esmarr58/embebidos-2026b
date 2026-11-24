/*
 * Comandos - control con lineas clave=valor desde la app.
 *
 *   led=0 | led=1      apaga / enciende el LED de la placa
 *   periodo=<ms>       cada cuanto se envia la linea de estado (0 = no enviar)
 *
 * Responde "ok ..." o "error ...". La linea de estado es una linea de datos
 * (led:<0|1>,cuenta:<n>) que la app puede mostrar en un label o graficar.
 *
 * En la ESP32-S3-DevKitC-1 el LED es RGB direccionable (GPIO48 o GPIO38 segun la
 * revision); digitalWrite(LED_BUILTIN, ...) del core lo maneja en blanco.
 */
#include <BLESerial.h>
#include <stdlib.h>

#ifdef LED_BUILTIN
static const uint8_t PIN_LED = LED_BUILTIN;
#else
static const uint8_t PIN_LED = 2;
#endif

static bool led = false;
static uint32_t periodoMs = 1000;
static uint32_t ultimoEstado = 0;
static uint32_t cuenta = 0;
static String linea;

// Convierte texto a entero sin signo; false si no es un numero completo.
static bool aEntero(const String &txt, uint32_t &valor)
{
    if (txt.length() == 0) {
        return false;
    }
    char *fin = nullptr;
    unsigned long v = strtoul(txt.c_str(), &fin, 10);
    if (*fin != '\0' || txt[0] == '-') {
        return false;
    }
    valor = (uint32_t)v;
    return true;
}

static void atender(String cmd)
{
    cmd.trim(); // quita espacios y el '\r' si la app manda "\r\n"
    if (cmd.length() == 0) {
        return;
    }
    int igual = cmd.indexOf('=');
    if (igual < 0) {
        BLESerial.printf("error se esperaba clave=valor: %s\n", cmd.c_str());
        return;
    }
    String clave = cmd.substring(0, igual);
    String valor = cmd.substring(igual + 1);
    clave.trim();
    valor.trim();
    uint32_t v = 0;

    if (clave == "led") {
        if (valor != "0" && valor != "1") {
            BLESerial.printf("error led acepta 0 o 1: %s\n", valor.c_str());
            return;
        }
        led = (valor == "1");
        digitalWrite(PIN_LED, led ? HIGH : LOW);
        BLESerial.printf("ok led=%d\n", led ? 1 : 0);
    } else if (clave == "periodo") {
        if (!aEntero(valor, v) || (v != 0 && (v < 50 || v > 60000))) {
            BLESerial.printf("error periodo en ms: 0 o 50..60000: %s\n", valor.c_str());
            return;
        }
        periodoMs = v;
        BLESerial.printf("ok periodo=%lu\n", (unsigned long)periodoMs);
    } else {
        BLESerial.printf("error comando desconocido: %s\n", clave.c_str());
    }
}

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
    BLESerial.begin("ESP32-Comandos");
}

void loop()
{
    // Arma la linea sin bloquear: puede llegar partida en varias notificaciones.
    while (BLESerial.available()) {
        char c = (char)BLESerial.read();
        if (c == '\n') {
            atender(linea);
            linea = "";
        } else if (linea.length() < 128) {
            linea += c;
        }
    }

    uint32_t ahora = millis();
    if (periodoMs > 0 && ahora - ultimoEstado >= periodoMs) {
        ultimoEstado = ahora;
        BLESerial.printf("led:%d,cuenta:%lu\n", led ? 1 : 0, (unsigned long)cuenta++);
    }
    delay(2);
}
