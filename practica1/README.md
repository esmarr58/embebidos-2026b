# Práctica 1 — Contador en display de 7 segmentos

Código **base** de la Práctica 1 del Seminario de Sistemas Embebidos (I7267).
Se explica en el TC1 (Clase 4) y **se entrega en la Clase 5**.

## Qué hace el código base

`main.c` muestra un contador **0 → 9** en un display de **7 segmentos de cátodo
común**, usando un decodificador **BCD → 7 segmentos** (la tabla `DIGITO[]`).

## Tu tarea

Agregar un **botón** (con **pull-down**) que cambie el **sentido** de la cuenta
(**ascendente ↔ descendente**). Los puntos a completar están marcados como `TODO`
en `main.c`. Puedes apoyarte en la IA, pero **entiende cada línea**.

## Hardware

- Placa **ESP32-S3-DevKitC-1**.
- Display de **7 segmentos, cátodo común**: el pin **común va a GND**.
- Segmentos `a..g` → **GPIO 4, 5, 6, 7, 15, 16, 17**, cada uno con su resistencia
  (~330 Ω). *(Evita 0,3,45,46 strapping · 26–37 flash · 19,20 USB.)*
- **Botón** en **GPIO 18** con **pull-down** de 10 kΩ: reposo = 0, presionado = 1.

> El número de cada pin del display **varía por modelo**: confírmalo en su hoja de datos.

## Cómo se usa (flujo de clase)

1. **No crees un proyecto nuevo**: abre tu **proyecto base** en PlatformIO.
2. Copia `main.c` en el `src/main.c` de tu proyecto base.
3. **Build** (✓) → **Upload** (→) → **Monitor** (🔌).
4. Completa los `TODO` del botón para cambiar el sentido de la cuenta.

Dr. Rubén Estrada Marmolejo · CUCEI, Universidad de Guadalajara
