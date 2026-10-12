# Ejemplo 4 — El número lo produce otra tarea (productor / consumidor)

- **Productor** `tarea_contador` (FreeRTOS, cada 250 ms): calcula 00–99, lo
  **codifica** en `pantalla[2]` (dos patrones) e imprime.
- **Consumidor** `refresco_cb` (esp_timer, cada 2.5 ms): solo **copia** el patrón y pinta.
- `pantalla[]` son **dos bytes**: se escriben y leen dentro de una **sección crítica**
  (`portMUX_TYPE` + `portENTER_CRITICAL` / `portEXIT_CRITICAL`). Sin ella, al pasar
  de 39 a 40 el callback podría leer un byte nuevo y otro viejo ("49" un instante).
- `app_main` crea el timer y la tarea y **termina**: ambos siguen vivos.

**Experimento del mito:** `#define CALLBACK_PESADO 1` mete un `printf` en el
callback (~3–4 ms a 115200 baud, más que el periodo): las llamadas se atrasan y
el barrido se vuelve irregular.

Diagrama: `diagrama-bloques-ej4.pdf`.
