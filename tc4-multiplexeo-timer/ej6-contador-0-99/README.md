# Ejemplo 6 — Contador 00–99 en el display multiplexado (reto del TC4)

Los **dos dígitos de 7 segmentos** muestran 00, 01, 02 … 99 y vuelven a 00,
**un número por segundo**. El número se ve en el **display**; el Monitor solo
lo repite como apoyo (`cuenta = 37`).

Dos `esp_timer` periódicos:

- `t_refresco` → `refresco_cb` cada `REFRESH_US` = 2500 µs: apaga → escribe →
  enciende → alterna (cada dígito se repinta cada 5 ms → **200 Hz**).
- `t_segundo` → `segundo_cb` cada `SEGUNDO_US` = 1 000 000 µs: `cuenta = (cuenta + 1) % 100`
  y **codifica** los dos patrones en `pantalla[2]`.

`cuenta` y `pantalla[]` se escriben y se leen dentro de una **sección crítica**
(`portMUX_TYPE candado` + `portENTER_CRITICAL` / `portEXIT_CRITICAL`), como en el
Ejemplo 4: al pasar de 39 a 40 nunca se ve «49». `app_main` no barre: solo imprime
la cuenta cuando cambia.

**Prueba:** cambia `SEGUNDO_US` a `100000` (10 cuentas por segundo) y comprueba que el
display sigue sin parpadear: la base de tiempo del conteo no afecta al refresco.

Diagrama: `diagrama-bloques-ej6.pdf`.
