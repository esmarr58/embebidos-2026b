# TC05 v2 — Dos displays, interrupción externa y light sleep

Seminario de Sistemas Embebidos I7267, 2026B. ESP32-S3 y ESP-IDF 5.5.0;
todos los proyectos fijan `platform = espressif32@6.12.0`.

La guía vigente en el repositorio de clases es `SistemasEmbebidos-2026B/TrabajosClase/TC5-Interrupciones-Guia-Evidencia-v3.docx`
y su PDF. Se entrega **esa guía llena en un solo PDF**. Los ejemplos anteriores
de LED, watchdog y deep sleep se conservan en `_archivo/v1-externa-wdt-sleep/`.

## Programas entregados

| Proyecto | Comportamiento |
|---|---|
| `programa-a-contador-isr` | Cuenta 00–99 solo por pulsaciones válidas, con antirrebote; 99 → 00. |
| `programa-b-light-sleep` | Cuenta cada segundo. Pulsar y soltar suspende; otra pulsación despierta y continúa desde la cuenta guardada. |
| B, entorno `control-despierto` | Apaga los displays y detiene los timers, pero no llama a sleep. Permite separar el efecto de los LED del de la suspensión. |

En B el tiempo suspendido **no se suma** a la cuenta: el siguiente incremento
ocurre un segundo después de reanudar. Se conserva RAM normal, sin reiniciar
`app_main`. No se usa deep sleep ni un temporizador para despertar.

## Mismo montaje de TC04 y P4

Dos dígitos de cátodo común, con bus compartido y 2N3904 por cátodo:

| Señal | GPIO / conexión |
|---|---|
| a,b,c,d,e,f,g | 4,5,6,7,15,16,17; resistencia de 220–330 ohm por segmento |
| Decenas / unidades | 10 / 11 → resistencia de base 1–2.2 kohm → 2N3904 |
| Transistores | Colector al cátodo común, emisor a GND; comprobar patillaje del fabricante |
| Botón | GPIO1 a GND; pull-up interna y resistencia externa de 10 kohm entre GPIO1 y 3V3 |
| Alimentación de medida | Fuente regulada 5 V, amperímetro en serie al pin VIN/5V y retorno a GND |

Los displays se excitan con GPIO de 3.3 V; VIN/5 V es alimentación de la placa,
no una señal de entrada para los GPIO. GPIO4 NO es botón en esta versión.
Si reutilizas cuatro displays de P4, desconecta sin alimentación los otros dos
cátodos de habilitación (GPIO12/13); usa solo los dos del TC04.

El refresco reutiliza TC04: `esp_timer`, 2500 us por cambio de dígito,
un dígito habilitado cada vez, 200 Hz por dígito. El callback corre en la
tarea `esp_timer`; la ISR externa es `boton_isr`, que solo notifica a la tarea.
El antirrebote pide 30 ms de nivel estable en muestras cada tick de FreeRTOS;
usa pulsaciones humanas de al menos 0.1 s y suelta entre ellas.

## Abrir y compilar

Código público: [TC05 en GitHub](https://github.com/esmarr58/embebidos-2026b/tree/main/tc5-interrupciones).
[Programa A](https://github.com/esmarr58/embebidos-2026b/tree/main/tc5-interrupciones/programa-a-contador-isr) ·
[Programa B](https://github.com/esmarr58/embebidos-2026b/tree/main/tc5-interrupciones/programa-b-light-sleep) ·
[Descargar ZIP del repositorio](https://github.com/esmarr58/embebidos-2026b/archive/refs/heads/main.zip).
Extraer todo; las carpetas están en `embebidos-2026b-main/tc5-interrupciones/`.

1. Desde la carpeta de entrega abre **el proyecto A o B completo** en VS Code / PlatformIO.
   Deben quedar visibles `platformio.ini`, `CMakeLists.txt` y `src/`.
2. En `src/main.c` cambia la primera línea por tu identificación; no alteres los pines.
3. Ejecuta Build, Upload y Monitor a 115200 para pruebas por USB.
4. En B, el entorno predeterminado `esp32-s3-devkitc-1` usa sleep real;
   `control-despierto` es solo el control de consumo, también hay que subirlo.
5. CLI desde el proyecto: `pio run`; `pio run -t upload`; `pio device monitor`.
   Para el control de B: `pio run -e control-despierto -t upload`.

No mezclar el `main.c` de A con el de B en el mismo `src/`. Los proyectos son
autónomos. La distribución en Dropbox incluye ambos y sus diagramas en PDF.

## Medición sin USB

Primero programa por USB y verifica el funcionamiento. Después corta alimentación,
retira **los dos USB** y conecta: fuente +5 V → borne de corriente del multímetro →
VIN/5V; fuente negativa → GND. El multímetro va en **serie**, nunca en paralelo a
la fuente. Comprueba función DC, bornes, fusible y rango; empieza en el rango alto
adecuado para el arranque y cambia de borne/rango solo sin alimentación.
No alimentar a la vez por USB y por VIN. No usar el pin 3V3 como entrada de 5 V.

La medida es la corriente total a 5 V de la placa con su montaje: regulador,
LED de alimentación, puente USB-UART si existe y displays, no solo el silicio.
No se exige un valor de catálogo en microamperios. Toma tres lecturas estables
en cada estado (activo, control despierto apagado, light sleep apagado) con
botón suelto, misma fuente y rango/resolución comparables. Guarda fotos de
borne/rango, conexión serie y lecturas, además de un video breve de reanudación.
Las capturas UART se hacen en una fase separada, sin la fuente externa conectada.

## Detalles de B que se deben poder explicar

- La pulsación de entrada se libera antes de dormir: despertar GPIO usa nivel bajo.
- El timer de refresco se detiene y los dos dígitos y segmentos se ponen en cero.
- Un candado protege el valor y las salidas. Una confirmación en la misma tarea
  `esp_timer` espera callbacks en curso antes de suspender; no hay ráfaga de refrescos.
- Tras despertar se desactiva el wakeup y se restaura `GPIO_INTR_NEGEDGE`, porque
  `gpio_wakeup_enable` cambia el detector a nivel bajo en esta versión de ESP-IDF.
- Se consume la liberación del botón de despertar antes de rearmar la ISR;
  mantenerlo presionado no vuelve a dormir inmediatamente.
- Si el sistema rechaza sleep, se imprime el error y se reanuda: ese intento
  no debe registrarse como una medición válida de sueño.

## Referencias oficiales

- [Sleep ESP32-S3, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/sleep_modes.html)
- [esp_timer, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/system/esp_timer.html)
- [Alimentación DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)

La compilación y los ensayos lógicos se registran en `VALIDACION-TC05-v2.txt`.
Las corrientes y el montaje físico los debe verificar el docente/alumno en laboratorio.
