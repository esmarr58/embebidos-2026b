# Ejemplo 3 — Multiplexeo gobernado por `esp_timer` (ejemplo resuelto del TC4)

Un `esp_timer` **periódico** llama a `refresco_cb` cada `REFRESH_US` = 2500 µs.
Cada llamada: **apaga** los dos dígitos → **escribe** el patrón del que toca →
lo **enciende** → **alterna**. Cada dígito se repinta cada 5 ms (**200 Hz**).

`app_main` hace a propósito un "trabajo pesado" de 300 ms (espera activa con
`esp_rom_delay_us`) y aun así el display **no parpadea**: el ritmo lo marca el
timer, no el bucle. Cada segundo imprime cuántas veces corrió el callback (~400).

- El callback corre en la **tarea `esp_timer`** (no es ISR): `gpio_set_level` es válido;
  `printf` y esperas, **no**.
- `valor` es `volatile uint8_t`: un byte se lee/escribe de un solo golpe.
- Experimento: `REFRESH_US 40000` → 12.5 Hz por dígito → parpadeo.

Diagrama: `diagrama-bloques-ej3.pdf`.
