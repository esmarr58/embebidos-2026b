# Práctica 4 — Reloj mm:ss en 4 displays de 7 segmentos (código BASE)

Código base de la **Práctica 4 (Timers)** del Seminario de Sistemas Embebidos
(I7267), 2026B. ESP32-S3-DevKitC-1 con **ESP-IDF 5.5.0** en PlatformIO
(`platform = espressif32@6.12.0`, `framework = espidf`).

Es el **80 % que se entrega funcionando**: el reloj `MM:SS` completo, con dos
`esp_timer` periódicos independientes y dos botones. La guía de evidencia
(`Practica04-Reloj-mmss-Guia-Evidencia`) lo usa en la **Parte 1 (guiada)**; en la
**Parte 2 (propia)** cada alumno lo modifica. Los comentarios
`TODO (tu modificacion)` marcan dónde suele entrar esa modificación.

## Qué hace

| Bloque | Dónde | Periodo | Qué hace |
|---|---|---|---|
| `t_refresco` → `refresco_cb` | tarea `esp_timer` | `REFRESH_US` = 2000 µs | apaga los 4 dígitos → escribe `PATRON[digito_de()]` → enciende uno → avanza `dig_activo` (0,1,2,3,0…). 4 × 2 ms = 8 ms por vuelta → **125 Hz por dígito**, duty 25 % |
| `t_tick` → `tick_cb` | tarea `esp_timer` | `SEGUNDO_US` = 1 000 000 µs | si `corriendo`: `segundos = (segundos + 1) % 6000` (99:59 → 00:00); mide `periodo_us` con `esp_timer_get_time()` |
| `app_main` | tarea principal | sondeo cada `SONDEO_MS` = 20 ms | `BTN_PAUSE` (flanco) alterna `corriendo`; `BTN_RESET` pone `segundos = 0`; imprime una línea por segundo |

Los datos compartidos (`segundos`, `corriendo`, `periodo_us`) se escriben dentro
de una **sección crítica** (`portMUX_TYPE candado` + `portENTER_CRITICAL` /
`portEXIT_CRITICAL`), como en el TC4.

### Monitor serie (115200)

```
Practica 4 - Reloj mm:ss. Refresco 2000 us (125 Hz por digito), base 1000000 us
t = 00:01 | periodo 1 s medido = 1000000 us | refrescos/s = 500
t = 00:02 | periodo 1 s medido = 1000001 us | refrescos/s = 500
Reloj en pausa
Reloj reiniciado a 00:00
```

- `periodo 1 s medido`: tiempo real entre dos llamadas a `tick_cb` (µs).
- `refrescos/s`: cuántas veces corrió `refresco_cb` en el último segundo
  (esperado 1 / 2 ms = **500**).

## Hardware (ESP32-S3)

Display de **cátodo común** (un `1` enciende el segmento), 4 dígitos `MM:SS`,
bus a–g compartido y un **2N3904** por dígito (interruptor de bajo lado).
Diagrama: `diagrama-conexiones-practica4.pdf`.

| Señal | GPIO | Conexión |
|---|---|---|
| Segmentos a, b, c, d | 4, 5, 6, 7 | GPIO → R 220–330 Ω → segmento (bus compartido) |
| Segmentos e, f, g | 15, 16, 17 | ídem |
| Habilita D0 (MM decenas) | 10 | GPIO → R_B 1–2.2 kΩ → base 2N3904; colector → cátodo D0; emisor → GND |
| Habilita D1 (MM unidades) | 11 | ídem |
| Habilita D2 (SS decenas) | 12 | ídem |
| Habilita D3 (SS unidades) | 13 | ídem |
| `BTN_PAUSE` | 1 | botón a GND (pull-up interna, activo en bajo) |
| `BTN_RESET` | 2 | botón a GND (pull-up interna, activo en bajo) |

Si solo consigues **2 dígitos**, conéctalos como D2 y D3 (segundos `SS`).

## Compilar

Proyecto PlatformIO **completo**: PIO Home → *Open Project* → esta carpeta →
**Build → Upload → Monitor**. En clase basta con copiar `src/main.c` a tu
proyecto base (un solo `main*.c` en `src/`). Pon tu clave de identidad en la
primera línea: `// P4 - Nombre Apellido - código - fecha`.

> Windows: si la ruta es larga y falla la compilación, copia la carpeta a una
> ruta corta (p. ej. `C:\p4`).

## Archivos

| Archivo | Qué es |
|---|---|
| `src/main.c` | Reloj `MM:SS` completo (base de 1 s + multiplexeo + botones + mediciones) |
| `diagrama-bloques-practica4.pdf` (+ `.tex`) | Diagrama a bloques del firmware |
| `diagrama-conexiones-practica4.pdf` (+ `.tex`) | Diagrama de conexiones |
| `platformio.ini`, `CMakeLists.txt`, `src/CMakeLists.txt` | Proyecto PlatformIO + ESP-IDF |

Los `.tex` son `standalone`: `pdflatex diagrama-bloques-practica4.tex`.
