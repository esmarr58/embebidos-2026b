# Práctica 05 — Contador óptico por interrupción (semiguiada)

**Base: 80 puntos. Extra funcional opcional: 20 puntos. Sin extra, máximo 80/100.**
Se suman los puntos obtenidos; no se convierte la base de 80 a escala de 100.

Código público: [carpeta P05](https://github.com/esmarr58/embebidos-2026b/tree/main/practica5-contador-optico)
· [proyecto base](https://github.com/esmarr58/embebidos-2026b/tree/main/practica5-contador-optico/base).
Extrae el ZIP completo de GitHub y abre `practica5-contador-optico/base/` en
VS Code/PlatformIO. `platform = espressif32@6.12.0`, ESP-IDF 5.5.0, sin Arduino.
La guía DOCX/PDF está en la entrega de Dropbox y en el tablero del curso.

## Qué se reutiliza y qué cambia

- Fototransistor y LED emisor de P3; colector con carga inicial 2.2 kohm a **3V3**,
  emisor a GND y colector a **GPIO8**, igual que la versión ADC anterior.
- LED emisor y su resistencia: mismo montaje funcional anterior. No aplicar
  5 V al colector ni a GPIO8, aunque el emisor luminoso se alimente a 5 V.
- Dos displays de cátodo común del TC04/TC05: segmentos 4,5,6,7,15,16,17;
  habilitaciones 10/11 mediante 2N3904 y sus resistencias. Refresco 2500 us.
- Esta base usa GPIO8 digital por **interrupción en ambos flancos**. El ADC de
  P3 sirve como antecedente de caracterización, no genera estas interrupciones
  ni convierte un nivel intermedio en una señal digital válida.

Primero medir haz libre/bloqueado y comprobar niveles digitales. Si el transistor
sigue en región activa, ajustar el montaje óptico/carga con el profesor o
acondicionar la señal antes de usarla como GPIO. El filtro temporal no crea
histéresis analógica ni arregla niveles eléctricos inválidos.

## Funcionamiento de la base (80 puntos)

`sensor_isr` toma nivel y tiempo y los coloca en una cola. La tarea reconstruye
los cambios en orden y confirma 2 ms de haz libre para armar y 2 ms bloqueado
para contar una vez. Un objeto sostenido no vuelve a contar hasta liberar el
haz el tiempo requerido. Las dos ventanas son configurables en `src/main.c`.
No se copia el antirrebote de botón de 30 ms. No se promete capturar cualquier
pulso: los de duración inferior al filtro se rechazan y el hardware/cola
también limitan la velocidad. Un ruido suficientemente largo puede contar.

El display muestra `total % 100` (99→00) y el monitor conserva el total de
eventos de la sesión. No hay reinicio de cuenta por botón en la base: para
iniciar otro ensayo registra el total inicial y usa la diferencia final−inicial,
o reinicia la placa y espera a que quede `estable=0 armado=1` sin objetos.
Un objeto presente al arrancar no inventa un cruce: se debe retirar antes.

Si se llena la cola se incrementa `perdidos`, se descarta la secuencia incompleta
y se exige un nuevo estado libre estable. **Con `perdidos` aumentado durante el
ensayo, ese conteo no es fiable:** registrarlo y repetir a menor velocidad o
corregir la causa. `rechazados` son transiciones cortas no confirmadas, no
necesariamente ruido: también puede haber objetos válidos demasiado rápidos.
`repetidos` señala avisos que leyeron el mismo nivel; tampoco es un contador
de objetos. Los tiempos se capturan por software en la ISR, no por captura
hardware. No hay sleep ni watchdog experimental obligatorio en la base.

## Pruebas de laboratorio

1. Medir y registrar tensión de colector con haz libre/bloqueado, carga y luz.
2. Build/Upload/Monitor (115200); esperar `estable=0 armado=1`.
3. Sostener objeto dos segundos: una cuenta. Retirar y repetir: otra cuenta.
4. Verificar 98→99→00→01 en displays y 98→99→100→101 en total.
5. Hacer tres ensayos de 20 pasos: anotar total inicial/final, diferencia,
   error respecto a 20 y variación de `perdidos`.
6. Repetir con tres velocidades/condiciones descritas; no inventar Hz para
   movimientos manuales. Informar el tiempo de ensayo y forma de pasar objetos.

## Extra opcional (20 puntos)

Una función propia por encima de esta base, con problema/objetivo, cambio de
código identificable y prueba antes/después. Orientaciones: meta y alarma,
conteo de lotes, registro/exportación, tasa de eventos con ventana definida,
o dirección con segundo sensor. No se exige comprar otro sensor ni elegir
una de esas ideas. Cambiar nombre, color, pines o ajustar los filtros para
que la base funcione no basta como extra.

Evaluación: objetivo/diferencia de la base 4, implementación 8, prueba y
explicación 8. Puede obtenerse parte de esos 20. No se exige extra para
entregar la base: marcar «sin extra» y dejar la sección opcional sin llenar.
Ejemplos: base 80 + extra 0 = 80; base 72 + extra 0 = 72; base 80 + extra 12 = 92.

## Archivos y verificaciones

- `base/src/main.c`: ISR, cola, tarea y displays; `logica_optica.h`: estado/filtro.
- `base/diagrama-bloques.pdf` y `diagrama-conexiones.pdf`: flujo y montaje.
- `tests/test_logica.py --cc <gcc>`: pruebas del filtro real en C, en escritorio.
- `VALIDACION-P05.txt`: compilación, pruebas y límites pendientes de laboratorio.

Referencias: [GPIO, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/gpio.html)
y [esp_timer](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/esp_timer.html).
