# TC06 — Chat por UART2 entre dos ESP32-S3

Seminario de Sistemas Embebidos I7267, 2026B. ESP32-S3 y ESP-IDF 5.5.0;
el proyecto fija `platform = espressif32@6.12.0`.

La guía vigente en el repositorio de clases es `SistemasEmbebidos-2026B/TrabajosClase/TC6-Chat-UART-Guia-Evidencia-v1.docx`
y su PDF. Se entrega **esa guía llena en un solo PDF**. Se trabaja **por parejas**
en la Sesión 22 (martes 3 de noviembre de 2026), después de la teoría de UART:
cada alumno trae su ESP32-S3-DevKitC-1 y su laptop; las dos tarjetas corren el
**mismo firmware**.

## Programa entregado

| Proyecto | Comportamiento |
|---|---|
| `programa-chat-uart2` | Lo que tecleas en el Monitor de PlatformIO (UART0, USB) sale por UART2 hacia la otra tarjeta como `nombre:texto\n`; lo que llega por UART2 se imprime como `[nombre] texto`. Eco local `[Yo] texto`. |
| Comandos en el Monitor | `/nombre Ana` apodo; `/ping` comprueba el enlace (responde `enlace UART2 OK`, `LOOPBACK` o `sin respuesta: revisa TX<->RX cruzados y GND`); `/baud 9600` cambia el baud de UART2 sin reprogramar; `/ayuda`. |
| Modo loopback | Una sola tarjeta con **GPIO17 puenteado a GPIO18**: `/ping` imprime `LOOPBACK` y cada mensaje vuelve como `[Eco]`. Sirve para probar sin compañero. |

Si un `/ping` no recibe `@pong` en 2 s, el firmware imprime
`sin respuesta: revisa TX<->RX cruzados (17->18, 18<-17) y GND comun`.
Los errores que detecta el hardware de UART2 (`UART_FRAME_ERR`, `UART_PARITY_ERR`,
`UART_BREAK`, `UART_FIFO_OVF`) se imprimen como `[sistema] ...`: así se ve qué
pasa al poner baudios distintos o quitar el GND.

## Conexiones

| Señal | GPIO / conexión |
|---|---|
| UART2 TX | **GPIO17** → GPIO18 (RX) de la **otra** tarjeta |
| UART2 RX | **GPIO18** ← GPIO17 (TX) de la **otra** tarjeta |
| Tierra | **GND ↔ GND** entre las dos tarjetas (obligatorio: cada una se alimenta de su propio USB) |
| UART0 | GPIO43/GPIO44 → puente CP2102 → USB → Monitor de PlatformIO (no se cablea) |
| Loopback | un solo cable GPIO17 ↔ GPIO18 de la misma tarjeta |

Tres cables Dupont hembra–hembra; ambas tarjetas son de 3.3 V, sin adaptador de
nivel. No unir 5V/VIN entre tarjetas. Conectar con las tarjetas sin alimentar.
UART2: 115200 bps, 8 bits de datos, sin paridad, 1 bit de paro (**8N1**);
tiempo de bit 8.68 µs, 10 bits por carácter → 86.8 µs por carácter.
Diagrama: `diagrama-conexiones-tc6.pdf`; diagrama a bloques del firmware en
`programa-chat-uart2/diagrama-bloques.pdf`.

## Cómo está hecho (lo que debes poder explicar)

- Se instala el driver de UART en **UART_NUM_0** (consola USB) y en **UART_NUM_2**
  (GPIO17/18), anillos de recepción de 1024 bytes.
- `tarea_teclado` lee UART0 con `uart_read_bytes` (timeout 20 ms), junta una línea
  hasta Enter, la manda por UART2 con `uart_write_bytes` y hace eco local `[Yo]`.
- `tarea_uart2` espera la **cola de eventos** del driver de UART2: `UART_DATA` junta
  bytes hasta `\n` y los interpreta; `UART_FRAME_ERR` y compañía se reportan.
- Consola y driver no chocan: en el S3 el log de ESP-IDF sale por UART0; tras
  instalar el driver se llama `uart_vfs_dev_use_driver(UART_NUM_0)`, con lo que
  `printf`/`ESP_LOG` también saldrían por el driver. El programa escribe todo con
  un solo helper `imprimir()` → `uart_write_bytes(UART_NUM_0, ...)` y agrega `\r\n`
  (el Monitor usa `monitor_filters = direct`, así que el CR+LF lo pone el firmware).
- Trama de texto por línea: `nombre:texto`; control `@hola`, `@soy`, `@ping`, `@pong`.
  El nombre por defecto es `S3-xxxx` (dos bytes de la MAC de fábrica), único por chip:
  por eso en loopback el firmware reconoce que el `@ping` que vuelve es el suyo.

## Abrir y compilar

Código público: [TC06 en GitHub](https://github.com/esmarr58/embebidos-2026b/tree/main/tc6-chat-uart).
[Programa](https://github.com/esmarr58/embebidos-2026b/tree/main/tc6-chat-uart/programa-chat-uart2) ·
[Descargar ZIP del repositorio](https://github.com/esmarr58/embebidos-2026b/archive/refs/heads/main.zip).
Extraer todo; la carpeta está en `embebidos-2026b-main/tc6-chat-uart/`.

1. Abre **`programa-chat-uart2` completo** en VS Code / PlatformIO. Deben quedar
   visibles `platformio.ini`, `CMakeLists.txt` y `src/`.
2. En `src/main.c` cambia la primera línea por tu identificación; no alteres los pines.
3. Build → Upload → Monitor a 115200. `platformio.ini` ya trae `monitor_echo = yes`,
   `monitor_eol = LF` y `monitor_filters = direct` para que el chat se vea limpio.
4. CLI desde el proyecto: `pio run`; `pio run -t upload --upload-port COMx`;
   `pio device monitor -p COMx`. Con dos tarjetas en la misma PC, dos terminales.
5. Primero `/ping`. Después escribe y Enter. `/nombre Ana` para que el otro vea tu apodo.

## Pruebas de la guía

| Prueba | Qué cambias | Qué debes ver |
|---|---|---|
| 1 Loopback | puente 17↔18 en tu tarjeta | `/ping` → `LOOPBACK`; mensaje → `[Eco]` |
| 2 Chat | tres cables a la otra tarjeta | `/ping` → `enlace UART2 OK`; `[Yo]` / `[nombre]` en ambas |
| 3 Baud distinto | `/baud 9600` en **una sola** tarjeta | `[?] ... trama sin formato`, `FRAME ERROR`, o nada; `/baud 115200` lo repara |
| 4 Sin GND | quitar solo el cable GND | tramas rotas o silencio; `sin respuesta` al `/ping` |
| 5 Osciloscopio (si hay) | sonda en GPIO17, GND de la tarjeta | bit de inicio en bajo, 8 bits LSB primero, bit de paro; 8.68 µs por bit |

## Capturas

`capturas/` guarda las capturas del Monitor para la guía y el tablero
(ver `capturas/LEEME.txt`: cómo tomarlas con dos tarjetas en una misma PC).

## Referencias oficiales

- [UART, ESP-IDF 5.5 (ESP32-S3)](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/uart.html)
- [Ejemplo `uart_events`, ESP-IDF 5.5](https://github.com/espressif/esp-idf/tree/v5.5/examples/peripherals/uart/uart_events)
- [Pines del ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)

Compilación real en PC1 (2026-10-27): platform espressif32 6.12.0,
framework-espidf 3.50500.0 (ESP-IDF 5.5.0), board esp32-s3-devkitc-1,
SUCCESS sin advertencias propias; RAM 13736 bytes, programa 247129 bytes.
Las pruebas con dos tarjetas se hacen en el aula.
