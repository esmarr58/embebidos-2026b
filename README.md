# embebidos-2026b

Banco de código **público** del Seminario de Sistemas Embebidos (I7267), 2026B.
Plataforma: **ESP32-S3-DevKitC-1** con **ESP-IDF** en **PlatformIO**.

> En clase **no** crees proyectos nuevos (la primera init descarga cientos de MB
> de toolchain/SDK). Reutiliza tu **proyecto base** ya descargado y solo cambia
> `src/main.c` por el archivo del trabajo correspondiente.

## Contenido

| Carpeta | Actividad |
|---|---|
| `tc1-guiada/` | TC1 — Proyecto base y LED (blink + PWM a mano) |
| `practica1/` | Práctica 1 — Contador en display de 7 segmentos (código base) |
| `practica3/` | Práctica 3 — Contador óptico 0–9 con fototransistor (entrada digital y ADC con calibración; port de Arduino a ESP-IDF) |
| `tc3-guiada/` | TC3 — Lector ADC de potenciómetro 10 kΩ a display de 7 segmentos (oneshot + calibración) |
| `tc4-multiplexeo-timer/` | TC4 — Multiplexeo de 2 dígitos de 7 segmentos por `esp_timer`: 5 ejemplos progresivos (PlatformIO `espressif32@6.12.0`, ESP-IDF 5.5.0) |
| `practica4-reloj-mmss/base/` | Práctica 4 — Reloj `MM:SS` en 4 displays de 7 segmentos con dos `esp_timer` (base de 1 s + refresco de 2 ms) y botones de pausa/reinicio: código base (80 %) con ganchos `TODO (tu modificacion)` para la parte propia |
| `tc5-interrupciones/` | TC05 — Dos displays: A contador por interrupción externa; B light sleep y control de corriente con VIN 5 V. |

Para TC05 abre el proyecto A o B completo dentro de `tc5-interrupciones/`; cada carpeta incluye su configuración y diagrama.

Dr. Rubén Estrada Marmolejo · CUCEI, Universidad de Guadalajara
