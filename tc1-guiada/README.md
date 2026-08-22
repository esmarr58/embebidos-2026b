# TC1 — Proyecto base y LED (código para el alumno)

Código de referencia del **TC1** del Seminario de Sistemas Embebidos (I7267).

## Cómo se usa (flujo de clase)

1. **No crees un proyecto nuevo** en clase: abre tu **proyecto base** ya
   descargado en PlatformIO (Open Folder).
2. Copia el contenido de `main.c` en el `src/main.c` de tu proyecto base.
3. **Build** (✓) → **Upload** (→) → **Monitor** (🔌).
4. Antes de sobrescribir para el siguiente trabajo, **respalda** tu `main.c`
   anterior (p. ej. `main_tc1.c`) para no perder lo que ya hiciste.

## Archivos

| Archivo | Nivel | Qué hace |
|---|---|---|
| `main.c` | 1 | Blink que **cede el CPU** con `vTaskDelay`. `f = 1000 / (2·SEMIPERIODO_MS)` Hz |
| `main_nivel2_pwm_a_mano.c` | 2 | PWM **a mano** con esperas ocupadas (busy-wait); controla el brillo con `DUTY_PCT`. A propósito está mal hecho: quema el CPU y obliga a `esp_task_wdt_deinit()`. |

## Hardware

- Placa **ESP32-S3-DevKitC-1**.
- LED externo en **GPIO 12** → resistencia 220–330 Ω → GND (pata larga = ánodo).

## Nota

La versión que además **barre** frecuencia y duty automáticamente está en
`tc1-blink.c` del repositorio del curso.
