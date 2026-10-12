# Ejemplo 2 — Dos dígitos multiplexados con `vTaskDelay`

El bucle de `app_main` apaga un dígito, escribe el otro, lo enciende y espera
`T_DIGITO_MS`; luego lo mismo con el otro dígito. **Funciona**, pero:

1. El bucle queda **ocupado** barriendo; cualquier trabajo extra alarga el barrido.
2. `vTaskDelay` cuenta en **ticks** (100 Hz por defecto → 10 ms). `pdMS_TO_TICKS(5)` = **0**.
3. El segundo se mide **contando vueltas**: si cambias el barrido, cambia el segundo.

**Experimentos**

| `T_DIGITO_MS` | Se repinta cada dígito | Qué se ve |
|---|---|---|
| 10 (por defecto) | 20 ms → 50 Hz | En el límite: parpadeo leve, sobre todo con visión periférica |
| 30 | 60 ms → 16.7 Hz | Parpadeo evidente |
| 5 | **0 ticks** | El bucle no cede la CPU: en el Monitor aparece el aviso del *Task Watchdog* (IDLE0) |

El Monitor imprime al inicio el tick real (`configTICK_RATE_HZ`) y cuántos ticks salen.
Diagrama: `diagrama-bloques-ej2.pdf`.
