# TC3 (guiada) — Lector ADC de un potenciómetro de 10 kΩ en un display de 7 segmentos

Código de **demostración en vivo** del TC3 del Seminario de Sistemas Embebidos
(I7267), Sesión 10. Un **potenciómetro de 10 kΩ** se lee con el **ADC1** del
ESP32-S3 (`adc_oneshot` + calibración `curve_fitting`, como en la Clase 9); el
valor `raw` (0–4095) se convierte a **mV** y a un **nivel 0–9** que se muestra
en el **display de 7 segmentos de cátodo común** de la Práctica 1.

## Qué hace (escalera de dificultad: `#define NIVEL`)

| `NIVEL` | Qué hace | Qué se ve |
|---|---|---|
| `0` | Solo ADC: lee `raw` e imprime `mV_ideal` (3300·raw/4096) y `mV_cal` (curva de fábrica). | Monitor: `raw=2048  mV_ideal=1650  mV_cal=1655` |
| `1` | `raw → nivel = raw·10/4096` (0–9) y lo muestra en el display. | El dígito cambia al girar el pot; Monitor: `raw / mV / nivel`. |
| `2` | Promedio de `N_MUESTRAS` + **histéresis** de `HISTERESIS` unidades de raw en la frontera. | El dígito **ya no baila** en la frontera entre dos niveles. |

- **Reto A** (`TODO`): parpadear el dígito 9 cuando `raw ≥ 4090` (saturación).
- **Reto B** (`TODO`): mostrar las decenas de porcentaje del voltaje calibrado (`mV / 330`).

El archivo viene con `NIVEL 0`; en clase se sube a `1` y a `2` cambiando solo esa línea.

## Hardware

| Señal | Pin ESP32-S3 | Conexión |
|---|---|---|
| Potenciómetro 10 kΩ (cursor) | **GPIO 10** (ADC1, `ADC_CHANNEL_9`) | extremo 1 → 3V3 · extremo 2 → GND · cursor → GPIO 10 |
| Display cátodo común, segmentos a–g | **GPIO 4, 5, 6, 7, 15, 16, 17** | cada segmento con R 330 Ω en serie; **COM → GND** |

- El cursor **debe** ir a un GPIO de **ADC1** (GPIO 1–10): ADC2 no funciona con Wi-Fi.
- GPIO 4–7 son ADC1_CH3–CH6, pero aquí son **salidas** del display: el pot **no** puede ir ahí.
  GPIO 3 es *strapping*. Queda **GPIO 10**, el canal del curso (Clase 9).
- Pines a evitar: 0, 3, 45, 46 (strapping) · 26–37 (flash/PSRAM) · 19, 20 (USB).
- **Nunca** más de 3.3 V en el pin del ADC: el pot va entre 3V3 y GND, no a 5 V.

## Mapeo raw → nivel

`4096 / 10 = 409.6` unidades de raw por dígito (≈ 330 mV por dígito con el modelo ideal).

| nivel | raw (desde–hasta) | mV ideal aprox. |
|---|---|---|
| 0 | 0 – 409 | 0 – 330 |
| 1 | 410 – 819 | 330 – 660 |
| 2 | 820 – 1228 | 660 – 990 |
| 3 | 1229 – 1638 | 990 – 1320 |
| 4 | 1639 – 2047 | 1320 – 1650 |
| 5 | 2048 – 2457 | 1650 – 1980 |
| 6 | 2458 – 2867 | 1980 – 2310 |
| 7 | 2868 – 3276 | 2310 – 2640 |
| 8 | 3277 – 3686 | 2640 – 2970 |
| 9 | 3687 – 4095 | 2970 – 3300 |

Con `HISTERESIS = 20`: estando en el nivel *k*, solo se sube cuando
`raw ≥ inicio(k+1) + 20` y solo se baja cuando `raw < inicio(k) − 20`.

## Cómo se usa (flujo de clase)

1. Abre tu **proyecto base** en PlatformIO desde **PIO Home → Open Project** (no crees uno nuevo).
2. Copia el contenido de `main.c` en el `src/main.c` de tu proyecto base.
3. **Build** (✓) → **Upload** (→) → **Monitor** (🔌 a 115200).
4. Gira el potenciómetro: en `NIVEL 0` cambian raw/mV; en `NIVEL 1` cambia el dígito; en `NIVEL 2` el dígito deja de bailar.

## Qué observar en el Monitor

- `NIVEL 0`: `raw` de 0 a ~4095 al girar; `mV_ideal` y `mV_cal` difieren poco cerca de 1 V y más cerca del tope (no linealidad, Clase 9).
- `NIVEL 1`: en la frontera (p. ej. raw ≈ 410) el nivel alterna 0/1 con el pot quieto: ruido de ±1–3 LSB.
- `NIVEL 2`: `raw_prom` más estable; la línea `--- cambio de nivel -> k ---` aparece solo al cruzar la frontera con margen.

## Parámetros para experimentar en clase

- `PERIODO_MS` — lecturas por segundo en el Monitor.
- `N_MUESTRAS` — 1, 16, 64: estabilidad frente a rapidez de respuesta.
- `HISTERESIS` — 0 (vuelve a bailar), 20, 100 (zona muerta demasiado grande: "se pega").
