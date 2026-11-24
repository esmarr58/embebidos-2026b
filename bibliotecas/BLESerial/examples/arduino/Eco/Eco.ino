/*
 * Eco - devuelve por BLE todo lo que recibe por BLE.
 *
 * Pruebalo con la app, nRF Connect o Serial Bluetooth Terminal: conectate a
 * "ESP32-Eco", activa las notificaciones de TX y escribe en RX.
 * Por USB (Serial) se informa cuando alguien se conecta o desconecta.
 */
#include <BLESerial.h>

static bool estabaConectado = false;

void setup()
{
    Serial.begin(115200);
    if (!BLESerial.begin("ESP32-Eco")) {
        Serial.println("No se pudo iniciar BLE");
    }
    Serial.println("Eco BLE listo");
}

void loop()
{
    // Se copia en bloques: un write por bloque, no uno por byte.
    int n = BLESerial.available();
    if (n > 0) {
        uint8_t buf[128];
        if (n > (int)sizeof(buf)) {
            n = sizeof(buf);
        }
        for (int i = 0; i < n; i++) {
            buf[i] = (uint8_t)BLESerial.read();
        }
        BLESerial.write(buf, n);
    }

    bool conectado = BLESerial.connected();
    if (conectado != estabaConectado) {
        estabaConectado = conectado;
        Serial.println(conectado ? "Cliente conectado" : "Cliente desconectado");
    }
    delay(2);
}
