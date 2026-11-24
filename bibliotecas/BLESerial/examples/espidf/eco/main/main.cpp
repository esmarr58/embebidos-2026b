/*
 * eco (ESP-IDF) - devuelve por BLE todo lo que recibe por BLE.
 * Usa la envoltura C++ (objeto BLESerial con los mismos nombres que en Arduino).
 */
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "BLESerial.h"

static const char *TAG = "eco";

extern "C" void app_main(void)
{
    if (!BLESerial.begin("ESP32-Eco")) {
        ESP_LOGE(TAG, "No se pudo iniciar BLE");
        return;
    }
    ESP_LOGI(TAG, "Eco BLE listo");

    bool estaba_conectado = false;
    for (;;) {
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
            BLESerial.write(buf, (size_t)n);
        }

        bool conectado = BLESerial.connected();
        if (conectado != estaba_conectado) {
            estaba_conectado = conectado;
            ESP_LOGI(TAG, "%s", conectado ? "Cliente conectado" : "Cliente desconectado");
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
