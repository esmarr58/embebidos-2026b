# TC07 — El RTC DS3231 por I2C desde el Monitor

Trabajo en clase **individual**, Sesión 26 (martes 17 de noviembre de 2026).
Cada alumno trae su módulo **DS3231** (ZS-042 u otro equivalente).
`platform = espressif32@6.12.0` → ESP-IDF 5.5.0, driver `i2c_master.h`, sin Arduino.

Abre `programa-rtc-consola/` completo en VS Code/PlatformIO (extrae el ZIP del
repositorio entero; no crees un proyecto nuevo). Build → Upload → Monitor.

## Conexiones (`diagrama-conexiones-tc7.pdf`)

| Módulo DS3231 | ESP32-S3-DevKitC-1 |
|---|---|
| VCC | **3V3** (no 5 V: las pull-ups del módulo van a VCC) |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |

GPIO8/9 quedan libres junto al display de la Práctica 4: este mismo montaje es la
base de la Práctica 6.

## Comandos de la consola

| Comando | Qué hace |
|---|---|
| `scan` | `i2c_master_probe` de 0x08 a 0x77 (DS3231 = 0x68, EEPROM AT24C32 = 0x57) |
| `hora` | lee 0x00..0x06: 7 bytes BCD crudos, tiempo de la lectura y hora decodificada |
| `auto` | hora cada segundo (se lee en una tarea, nunca en una ISR) |
| `poner AAAA-MM-DD hh:mm:ss` | escribe puntero + 7 bytes en una transacción; calcula el día de la semana (domingo = 1) y borra OSF |
| `temp` | registros 0x11 (con signo) y 0x12 (cuartos de grado) |
| `osf` | registro 0x0F y el bit OSF (¿se detuvo el oscilador?) |
| `regs` | vuelco de 0x00..0x12 |
| `dir 0x69` | cambia la dirección del dispositivo para provocar NACK |
| `vel 100` / `vel 400` | reloj SCL en kHz |
| `reset` | `i2c_master_bus_reset`: libera un esclavo pegado |

Los errores se imprimen con `esp_err_to_name` y la causa probable; el programa no
usa `ESP_ERROR_CHECK` al leer para que un NACK no reinicie la tarjeta.

## Archivos

- `programa-rtc-consola/src/main.c`, `platformio.ini`, `CMakeLists.txt`, `sdkconfig.defaults`.
- `programa-rtc-consola/diagrama-bloques.pdf`: consola → comandos → driver → DS3231.
- `diagrama-conexiones-tc7.pdf`: montaje y puntos de medición.

Compilado con `espressif32@6.12.0` (framework-espidf 3.50500.0 = 5.5.0) sin advertencias.

Referencias: [I2C, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/i2c.html)
· [DS3231](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf)
· [UM10204](https://www.nxp.com/docs/en/user-guide/UM10204.pdf).
