# TC4 — Multiplexeo de 2 dígitos de 7 segmentos gobernado por un timer

Serie progresiva de **6 proyectos PlatformIO + ESP-IDF completos** que acompaña al
deck de teoría *Clase 16 · Multiplexeo por timer* y al TC4 del Seminario de
Sistemas Embebidos (I7267), 2026B.

**Versión fijada:** `platform = espressif32@6.12.0` → **ESP-IDF 5.5.0**
(`framework-espidf 3.50500.0`). Todos compilan con `pio run` (SUCCESS).

| Carpeta | Idea | Lo nuevo |
|---|---|---|
| `ej1-un-digito/` | Un dígito cuenta 0–9 (sin multiplexeo) | Tabla de patrones y bus a–g |
| `ej2-multiplexeo-vtaskdelay/` | 2 dígitos barridos con `vTaskDelay` | Funciona… pero el bucle queda ocupado; ticks de 10 ms |
| `ej3-multiplexeo-esp-timer/` | **Ejemplo resuelto del TC4** | `esp_timer` periódico: apagar → escribir → encender → alternar |
| `ej4-valor-desde-otra-tarea/` | El número lo produce otra tarea | Buffer de pantalla, sección crítica, callback corto |
| `ej5-ghosting-y-brillo/` | Rebanadas de 500 µs | Brillo por tiempo encendido y orden anti-*ghosting* |
| `ej6-contador-0-99/` | **Reto del TC4:** el display cuenta 00–99, uno por segundo | Dos `esp_timer`: refresco 2.5 ms + conteo 1 s; sección crítica |

Cada carpeta trae `platformio.ini`, `CMakeLists.txt`, `src/CMakeLists.txt`,
`src/main.c`, su **diagrama a bloques en PDF** (y su fuente `.tex`) y un README.

## Etiquetas de subobjetivo (las mismas en los 6 `main.c`)

`[1]` configurar salidas · `[2]` codificar número → patrón · `[3]` crear/arrancar el timer ·
`[4]` refrescar: apagar → escribir → encender → alternar · `[5]` actualizar el valor

## Hardware (igual en todos)

Display de **cátodo común** (un `1` enciende el segmento) y un **2N3904** por dígito.

| Señal | GPIO | Conexión |
|---|---|---|
| Segmentos a, b, c, d | 4, 5, 6, 7 | GPIO → R 220–330 Ω → segmento (bus compartido) |
| Segmentos e, f, g | 15, 16, 17 | ídem |
| Habilita decenas | 10 | GPIO → R_B 1–2.2 kΩ → base 2N3904; colector → cátodo; emisor → GND |
| Habilita unidades | 11 | ídem |

## Cómo usarlo

- **Proyecto completo:** PIO Home → *Open Project* → elige la carpeta del ejemplo → Build → Upload → Monitor (115200).
  La primera vez PlatformIO descarga la plataforma 6.12.0 y ESP-IDF 5.5.0 (cientos de MB).
- **En clase (sin descargar nada):** copia solo `src/main.c` al `src/main.c` de tu proyecto base.

> Aviso esperado al compilar: `Flash memory size mismatch detected. Expected 8MB, found 2MB!`
> (el `sdkconfig` por defecto declara 2 MB; no afecta a estos ejemplos).

Dr. Rubén Estrada Marmolejo · CUCEI, Universidad de Guadalajara
