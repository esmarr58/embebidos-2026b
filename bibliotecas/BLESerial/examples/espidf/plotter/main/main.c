/*
 * plotter (ESP-IDF) - emite dos series cada 100 ms en el formato del Serial Plotter:
 *     seno:<v>,rampa:<v>\n
 */
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ble_serial.h"

#define PERIODO_MS 100

static const char *TAG = "plotter";

void app_main(void)
{
    ESP_ERROR_CHECK(ble_serial_init("ESP32-Plotter"));
    ESP_LOGI(TAG, "Plotter BLE listo");

    TickType_t ultimo = xTaskGetTickCount();
    uint32_t muestra = 0;
    for (;;) {
        // Seno de 0.5 Hz y amplitud 1; rampa de 0 a 99 que vuelve a 0.
        float t = (float)muestra * (PERIODO_MS / 1000.0f);
        float seno = sinf(2.0f * (float)M_PI * 0.5f * t);
        int rampa = (int)(muestra % 100);
        muestra++;

        // Sin conexion no bloquea: los bytes se descartan.
        ble_serial_printf("seno:%.3f,rampa:%d\n", (double)seno, rampa);

        vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(PERIODO_MS));
    }
}
