# Ejemplo 5 — Ghosting y brillo por tiempo encendido

El timer corre cada **500 µs** y divide el turno de cada dígito en **10 rebanadas**
(5 ms por dígito → 100 Hz por dígito):

- slot 0: apagar → escribir → encender el dígito que toca
- slot = `brillo`: apagar (el resto del turno queda oscuro)
- slot 9: pasar al otro dígito

`app_main` hace una rampa de `brillo` 1 → 10 → 1 (10 %–100 % del turno) y la imprime.

**Ghosting:** con `#define ORDEN_CORRECTO 0` se escriben los segmentos nuevos
**mientras el dígito anterior sigue encendido**; durante esos microsegundos aparece
una sombra tenue en segmentos que deberían estar apagados (se nota a oscuras).

Diagrama (con cronograma de un turno): `diagrama-bloques-ej5.pdf`.
