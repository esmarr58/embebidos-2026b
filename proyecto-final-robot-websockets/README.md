# Proyecto final — Robot móvil controlado por WebSockets

**Base: 80 puntos. Extra propio opcional: 20 puntos. Sin extra, máximo 80/100.**
Se suman los puntos obtenidos; la base de 80 no se convierte a escala de 100.
Presentación en clase: jueves 19 de noviembre de 2026. Entrega y defensa: **jueves 10 de diciembre de 2026**. Entrega individual.

Código público: [carpeta del proyecto](https://github.com/esmarr58/embebidos-2026b/tree/main/proyecto-final-robot-websockets)
· [proyecto base](https://github.com/esmarr58/embebidos-2026b/tree/main/proyecto-final-robot-websockets/base).
Extrae el ZIP completo de GitHub y abre `proyecto-final-robot-websockets/base/` en
VS Code/PlatformIO. `platform = espressif32@6.12.0`, ESP-IDF 5.5.0, sin Arduino.
La guía DOCX/PDF con el armado paso a paso y la rúbrica está en el tablero del curso.

## Kit

ESP32-S3-DevKitC-1 · módulo L298N · chasis con 2 motores TT y rueda loca ·
2 pilas 18650 + portapilas de 2 en serie · interruptor · voltímetro digital ·
cables Dupont y cable rojo/negro para potencia.

## Conexiones (ver `diagrama-conexiones.pdf`)

| L298N | ESP32-S3 | Función |
|---|---|---|
| ENA (sin jumper) | GPIO9 | PWM motor A (MCPWM0A, 1 kHz) |
| IN1 / IN2 | GPIO11 / GPIO12 | sentido motor A (izquierdo) |
| IN3 / IN4 | GPIO13 / GPIO14 | sentido motor B (derecho) |
| ENB (sin jumper) | GPIO10 | PWM motor B (MCPWM0B, 1 kHz) |
| +5V (jumper 5V-EN puesto) | 5V | alimenta la tarjeta desde las pilas |
| GND | GND | tierra común obligatoria |

Pilas (7.4 V nominal, 8.4 V cargadas) → interruptor → **+12V (VS)** del L298N;
negativo → GND del L298N. El voltímetro va en paralelo después del interruptor
(rojo a +, negro a −; si tiene 3 hilos, el amarillo también a +).
IN1–IN4 son los mismos pines del robot de Diseño de Interfaces.

**Antes de conectar el USB para programar, apaga el interruptor o retira el cable
de 5V.** No alimentes la tarjeta por 5V y por USB al mismo tiempo.

## Cómo se controla

El robot crea su propia red WiFi; no se necesita internet ni router.

- Red: `ROBOT-S3-xxxx` (el Monitor la imprime al arrancar; xxxx sale de la MAC).
- Clave: `embebidos2026`
- Servidor: `ws://192.168.4.1/ws` (en `http://192.168.4.1/` hay una página de prueba).

Usa cualquier app cliente de WebSocket (Android, iOS, o en la PC Postman o `websocat`).
Un comando de texto por mensaje; el robot contesta `ok: ...` o `error: ...`.

| Comando | Qué hace |
|---|---|
| `adelante [ms]`, `atras [ms]` | avanza o retrocede; con ms (50–10000) se detiene solo |
| `izq [ms]`, `der [ms]` | gira sobre su eje |
| `alto` | detiene los dos motores |
| `vel <0-100>` | ciclo de trabajo del PWM; cambia al vuelo |
| `prueba a`, `prueba b` | mueve un solo motor 1 s hacia adelante (revisar cableado) |
| `estado`, `ayuda` | estado del robot y lista de comandos |

Atajos: `w` `s` `a` `d` `x`. Por seguridad el robot va a **ALTO** si se cierra
el WebSocket o si el teléfono sale de la red del robot. Si una rueda gira al revés
con `prueba a`/`prueba b`, invierte sus dos cables en el L298N.

## Base (80 puntos)

Robot armado según los diagramas, alimentado por las pilas (sin USB), con el
voltaje medido, conectado a su red y controlado desde la app con todos los
comandos de la tabla. El desglose está en la rúbrica de la guía.

## Extra propio (20 puntos)

Una función que tú propones y programas encima de la base, con problema/objetivo,
cambio de código identificable (busca `TODO (tu extra)` en `src/main.c`) y prueba
antes/después. Ideas: medir la batería por ADC con divisor y enviarla en `estado`,
rampa de aceleración, coreografía con una secuencia de comandos, LED de estado,
ultrasónico que frene ante un obstáculo, ajuste fino por motor para que avance
recto, o tu propia interfaz. Cambiar nombres, pines o la clave no cuenta como extra.

Evaluación: objetivo y diferencia con la base 4, implementación 8, prueba y
explicación 8. Puede obtenerse parte de esos 20. Sin extra: marcar «sin extra».
Ejemplos: base 80 + extra 0 = 80; base 72 + extra 0 = 72; base 80 + extra 14 = 94.

## Archivos

- `base/src/main.c`: SoftAP, servidor WebSocket, intérprete de comandos, motores y paro de seguridad.
- `base/diagrama-bloques.pdf` y `base/diagrama-comandos.pdf`: flujo del firmware y estados/comandos.
- `diagrama-conexiones.pdf`: armado completo (pilas, interruptor, voltímetro, L298N, motores y ESP32-S3).

Referencias: [HTTP Server y WebSocket, ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/protocols/esp_http_server.html),
[WiFi SoftAP](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/wifi.html)
y [MCPWM](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/peripherals/mcpwm.html).
