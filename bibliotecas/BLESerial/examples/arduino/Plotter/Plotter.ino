/*
 * Plotter - emite dos series cada 100 ms en el formato del Serial Plotter:
 *     seno:<v>,rampa:<v>\n
 * La app las grafica; tambien sirve el Serial Plotter de Arduino por USB.
 */
#include <BLESerial.h>
#include <math.h>

static const uint32_t PERIODO_MS = 100;
static uint32_t siguiente = 0;
static uint32_t muestra = 0;

void setup()
{
    Serial.begin(115200);
    BLESerial.begin("ESP32-Plotter");
}

void loop()
{
    uint32_t ahora = millis();
    if ((int32_t)(ahora - siguiente) < 0) {
        return;
    }
    siguiente = ahora + PERIODO_MS;

    // Seno de 0.5 Hz y amplitud 1; rampa de 0 a 99 que vuelve a 0.
    float t = muestra * (PERIODO_MS / 1000.0f);
    float seno = sinf(2.0f * (float)M_PI * 0.5f * t);
    int rampa = (int)(muestra % 100);
    muestra++;

    // Sin conexion printf no bloquea: los bytes se descartan.
    BLESerial.printf("seno:%.3f,rampa:%d\n", seno, rampa);
    Serial.printf("seno:%.3f,rampa:%d\n", seno, rampa);
}
