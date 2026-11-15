# Práctica 06 — Reloj hh:mm con el RTC DS3231 (I2C) — semiguiada

**Base: 80 puntos. Extra funcional opcional: 20 puntos. Sin extra, máximo 80/100.**
Se suman los puntos obtenidos; no se convierte la base de 80 a escala de 100.

Abre `base/` completo en VS Code/PlatformIO (extrae el ZIP del repositorio entero).
`platform = espressif32@6.12.0` → ESP-IDF 5.5.0, driver `i2c_master.h`, sin Arduino.
La guía DOCX/PDF está en el tablero del curso.

## Hardware (`diagrama-conexiones-practica6.pdf`)

El **mismo** montaje de la Práctica 4 más el módulo DS3231 del TC07:

| Señal | GPIO |
|---|---|
| Segmentos a, b, c, d / e, f, g | 4, 5, 6, 7 / 15, 16, 17 (una R de 220–330 Ω por segmento) |
| Dígitos D0–D3 (2N3904, R_B 1–2.2 kΩ) | 10, 11, 12, 13 |
| Botón VISTA / FECHA (a GND) | 1 / 2 |
| DS3231: VCC / GND / SDA / SCL | 3V3 / GND / 8 / 9 |

## Funcionamiento de la base (80 puntos)

- `tarea_rtc` lee 0x00–0x06 y el registro de estado 0x0F cada 250 ms; convierte BCD a
  decimal y deja la hora en memoria compartida (candado `portMUX`). Imprime una línea por
  segundo: hora, tiempo de lectura, refrescos por segundo y errores.
- `refresco_cb` (`esp_timer`, 2000 µs) **solo pinta**: nunca usa el I2C. Si el RTC no
  responde o OSF = 1, muestra guiones sin dejar de refrescar.
- `tarea_consola`: `poner AAAA-MM-DD hh:mm:ss` (escribe 8 bytes y borra OSF), `temp`, `osf`, `ayuda`.
- Botones: GPIO1 alterna hh:mm ↔ mm:ss; GPIO2 muestra la fecha dd mm durante 3 s.

No hay timer de 1 s: la hora la lleva el DS3231 con su pila, así que sobrevive a un
reinicio o a quitar el USB (lo que la Práctica 4 no podía).

## Extra opcional (20 puntos)

Una función propia por encima de la base, con objetivo, cambio identificable y prueba
antes/después. Orientaciones: alarma hh:mm (software o registros 0x07–0x0D + INT/SQW),
ajuste de la hora con botones, formato 12 h, SQW a 1 Hz como interrupción, temperatura
en el display, cronómetro o registro en la EEPROM 0x57. Los comentarios
`TODO (tu extra)` de `main.c` marcan dónde suelen entrar.
Evaluación: objetivo 4, implementación 8, prueba y explicación 8.
Ejemplos: base 80 + extra 0 = 80; base 72 + extra 0 = 72; base 80 + extra 12 = 92.

## Archivos

- `base/src/main.c`, `platformio.ini`, `CMakeLists.txt`, `sdkconfig.defaults`.
- `base/diagrama-bloques.pdf` y `diagrama-conexiones-practica6.pdf`.

Compilado con `espressif32@6.12.0` (framework-espidf 5.5.0) sin advertencias.

Referencias: [I2C, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2c.html)
· [esp_timer](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/esp_timer.html)
· [DS3231](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf).
