# TC2 (guiada) — Antirrebote + PWM (MCPWM) para brillo de LED

Código de **demostración en vivo** del TC2 del Seminario de Sistemas Embebidos
(I7267). Un push button con resistencia **pull-down** cambia la **intensidad**
de un LED mediante **PWM por hardware (MCPWM)**.

## Qué hace

- Cada **pulsación confirmada por antirrebote** sube el brillo un escalón
  (`PASO_DUTY = 20 %`); al pasar del 100 % regresa a 0 % (cíclico).
- El PWM lo genera el periférico **MCPWM** (`driver/mcpwm.h`), igual que en la
  Teoría 10 (Clase 31) y la Práctica 9.
- El **antirrebote** es por *polling* con contador de estabilidad
  (`DEBOUNCE_MS / POLL_MS` ciclos), igual que en las Entradas Digitales (TC3).

## Hardware

| Señal | Pin ESP32-S3 | Conexión |
|---|---|---|
| LED (PWM) | **GPIO 12** (MCPWM0A) | GPIO 12 → R 220 Ω → LED (ánodo) → cátodo → GND |
| Botón | **GPIO 4** | 3V3 → push button → nodo; nodo → GPIO 4; nodo → R 10 kΩ → GND |

El pull-down externo (10 kΩ) mantiene el pin en **0** en reposo; al presionar,
el pin se conecta a 3V3 y se lee **1** (flanco ascendente).

## Cómo se usa (flujo de clase)

1. Abre tu **proyecto base** en PlatformIO (no crees uno nuevo).
2. Copia el contenido de `main.c` en el `src/main.c` de tu proyecto base.
3. **Build** (✓) → **Upload** (→) → **Monitor** (🔌 a 115200).
4. Presiona el botón: el LED sube de brillo por escalones.

## Parámetros para experimentar en clase

- `FREC_LED` — frecuencia del PWM (prueba 100 Hz vs 5000 Hz: parpadeo vs brillo suave).
- `PASO_DUTY` — cuánto sube el brillo por pulsación (número de niveles).
- `DEBOUNCE_MS` — pon `0` para ver los rebotes; súbelo para filtrarlos.
