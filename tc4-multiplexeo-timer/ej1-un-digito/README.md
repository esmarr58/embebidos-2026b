# Ejemplo 1 — Un dígito (sin multiplexeo)

El dígito de **unidades** cuenta 0–9, un número por segundo. Su 2N3904 queda
**siempre habilitado** (GPIO 11 = 1): todavía no hay barrido.

**Para qué sirve:** comprobar el cableado del bus a–g y la tabla `PATRON[]`
antes de multiplexar. Si un número sale mal, el error está en el cableado o en
el patrón, no en el timer.

- Subobjetivos usados: `[1]` salidas, `[2]` codificar, `[5]` valor.
- Monitor: `Ej1: muestro 3 (patron 0x4F)`.
- Diagrama: `diagrama-bloques-ej1.pdf`.

**Predice:** si el cable del segmento **g** (GPIO 17) queda suelto, ¿qué dígitos se siguen viendo bien? ¿Cuál se confunde con el 0?
