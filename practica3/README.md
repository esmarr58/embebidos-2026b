# Práctica 3 — Contador óptico 0–9 (fototransistor + display de 7 segmentos)

Código base de la **Práctica 3** del Seminario de Sistemas Embebidos (I7267),
portado a **ESP-IDF 5.5 (PlatformIO, `framework = espidf`)** desde el firmware de
Arduino de la Práctica 2 de Optoelectrónica (V3736) —
[`esmarr58/programas-optoelectronica`](https://github.com/esmarr58/programas-optoelectronica).

Un LED emisor ilumina de forma continua a un **fototransistor** (barrera
óptica). Cada objeto que **corta el haz** suma **+1** a una cuenta **0 → 9 → 0**
que se muestra en el **display de 7 segmentos de cátodo común** de la Práctica 1
(mismos pines, misma tabla de segmentos).

## Las dos versiones

| Archivo | Modo del transistor | Cómo decide 0/1 | API que se aprende |
|---|---|---|---|
| `main_digital.c` | **Corte y saturación** | El **hardware**: el GPIO ya recibe una señal cuadrada → `gpio_get_level()` | GPIO de entrada con pull interno, antirrebote por *polling*, `esp_timer_get_time()` |
| `main_adc.c` | **Región activa** | El **software**: se lee el voltaje con el ADC y se compara con **dos umbrales (histéresis)** | `adc_oneshot` + calibración *curve fitting* (mV reales), comparador con histéresis |

`main.c` es una **copia de `main_digital.c`**: es la versión que se carga
primero en la práctica. Se mantienen **dos archivos completos** (y no un solo
`main.c` con `#if MODO_ADC`) para que cada versión se lea de arriba abajo sin
saltos de preprocesador, igual que los dos sketches de Arduino originales.

- **Corte/saturación:** con luz suficiente el transistor satura
  (V_CE ≈ 0.2–0.8 V → **0**) y sin luz entra en corte (V_CE ≈ 3.3 V → **1**).
  Basta leer el pin digital.
- **Región activa:** el transistor **no** satura; V_CE = V_CC − I_C·R_L varía de
  forma continua con la luz. Se lee con el ADC en **milivoltios** y **tú** fijas
  los umbrales en el código, viéndolos en el monitor serie (la práctica pide
  medir V_out con haz libre y con haz bloqueado: el programa imprime el mínimo y
  el máximo observados).

## Hardware

| Señal | Pin ESP32-S3 | Conexión |
|---|---|---|
| Segmento **a** | **GPIO 4** | GPIO → R 220–330 Ω → segmento a |
| Segmento **b** | **GPIO 5** | GPIO → R 220–330 Ω → segmento b |
| Segmento **c** | **GPIO 6** | GPIO → R 220–330 Ω → segmento c |
| Segmento **d** | **GPIO 7** | GPIO → R 220–330 Ω → segmento d |
| Segmento **e** | **GPIO 15** | GPIO → R 220–330 Ω → segmento e |
| Segmento **f** | **GPIO 16** | GPIO → R 220–330 Ω → segmento f |
| Segmento **g** | **GPIO 17** | GPIO → R 220–330 Ω → segmento g |
| Común del display | **GND** | cátodo común (un **1** en el GPIO enciende el segmento) |
| Sensor — versión **digital** | **GPIO 18** | colector del fototransistor (entrada digital) |
| Sensor — versión **ADC** | **GPIO 8** (ADC1 canal 7) | colector del fototransistor (entrada analógica) |
| LED emisor | 5V | 5V → R1 330 Ω → ánodo; cátodo → GND |

Todos los pines son **seguros** en el ESP32-S3: ninguno es de *strapping*
(0, 3, 45, 46), de USB nativo (19, 20) ni de flash/PSRAM (26–37).

**Por qué GPIO 8 en la versión ADC:** el GPIO 18 de la versión digital pertenece
al **ADC2**, que comparte hardware con Wi-Fi y no se usa en el curso; los
canales del **ADC1** son los **GPIO 1–10** (GPIO *n* = canal *n−1*). Se eligió el
**GPIO 8 = ADC1_CH7** porque es el mismo pin que usa `contador-region-activa.ino`
en el repositorio de Optoelectrónica, así el cableado coincide entre las dos
materias. Si quieres usar **un solo cable** para las dos versiones, conecta el
colector al GPIO 8 y en `main_digital.c` cambia `PIN_SENSOR` a `8` (el GPIO 8
también sirve como entrada digital).

### Esquema (fototransistor en emisor común)

```
   3V3 ──[ R_L 2.2 kΩ ]──┬── colector (C) ──── GPIO 18 (digital)  ó  GPIO 8 (ADC)
                         │
                   (fototransistor)
                         │
   GND ──────────────────┴── emisor (E)

   5V ──[ R1 330 Ω ]── ánodo LED emisor ── cátodo ── GND
```

- Haz **presente** → el transistor conduce → colector **bajo** (0 / pocos mV).
- Haz **interrumpido** → corte → colector **≈ 3.3 V** (1 / ~3300 mV).

> **Alimenta el fototransistor a 3.3 V, NO a 5 V.** El colector va directo a
> un GPIO del ESP32-S3 (máx. 3.3 V) y el ADC mide 0–3.3 V. Verifica la tierra
> común antes de energizar.

## Cómo se usa (flujo de clase)

1. Abre tu **proyecto base** en PlatformIO (no crees uno nuevo).
2. Copia el contenido de **`main_digital.c`** (o de `main.c`, que es igual) en
   el `src/main.c` de tu proyecto base. Deja **un solo** `main*.c` en `src/`:
   el `CMakeLists.txt` del proyecto base compila *todo* lo que haya ahí.
3. **Build** (✓) → **Upload** (→) → **Monitor** (🔌 a 115200).
4. Pasa objetos por el haz: el display cuenta 0 → 9 → 0 y el monitor imprime
   cada detección con su marca de tiempo.
5. Para la parte de **región activa**, repite los pasos 2–3 con **`main_adc.c`**
   (mueve el cable del colector al **GPIO 8**).

## Qué observar en el monitor

Versión digital:

```
Practica 3 - Contador optico 0-9 (MODO DIGITAL: corte/saturacion)
Sensor en GPIO 18, NIVEL_HAZ_LIBRE = 0, antirrebote = 30 ms. Cuenta = 0
[  2140 ms] Objeto detectado. Cuenta = 1
[  3872 ms] Objeto detectado. Cuenta = 2
```

Versión ADC (una línea de traza cada 250 ms para calibrar):

```
Practica 3 - Contador optico 0-9 (MODO ADC: region activa)
Sensor en GPIO 8 (ADC1 canal 7), umbrales 1800/2500 mV, antirrebote 30 ms. Cuenta = 0
raw= 1105  V=  912 mV  | presente     | min= 905 mV (libre)  max= 912 mV (bloqueado)
[  4310 ms] Objeto detectado (raw=3925, 3212 mV). Cuenta = 1
raw= 3921  V= 3209 mV  | INTERRUMPIDO | min= 905 mV (libre)  max=3215 mV (bloqueado)
```

Los valores `min`/`max` son los **mV con haz libre** y **con haz bloqueado**
que pide la práctica; compáralos con el multímetro en el nodo del colector.

## Parámetros que quizá ajustes (una línea cada uno)

| Macro | Archivo | Para qué |
|---|---|---|
| `NIVEL_HAZ_LIBRE` (0/1) | `main_digital.c` | Nivel del GPIO con el haz libre. Leyendo el **colector** (cableado de la práctica) va en **0**; si lees el **emisor**, ponlo en **1**. La pull interna (`GPIO_PULLDOWN_ONLY` / `GPIO_PULLUP_ONLY`) se ajusta sola. |
| `DEBOUNCE_MS` | ambos | Ventana de antirrebote (20–50 ms). Pon `0` para ver los rebotes. |
| `UMBRAL_ALTO_MV` / `UMBRAL_BAJO_MV` | `main_adc.c` | Umbrales con histéresis. **Ajústalos con tu medición:** `BAJO` un poco arriba de los mV con haz presente, `ALTO` un poco abajo de los mV con haz interrumpido. |
| `TRAZA_MS` | `main_adc.c` | Cada cuánto se imprime raw/mV (250 ms). |

## Antirrebote

Las dos versiones usan el **antirrebote por *polling* con contador de
estabilidad** del TC2: la lectura se muestrea cada `POLL_MS = 10 ms` (un tick
de FreeRTOS) y un nuevo estado se acepta solo si se repite
`DEBOUNCE_MS / POLL_MS = 3` veces seguidas. En la versión ADC el antirrebote se
aplica **después** de la histéresis, sobre el estado ya binarizado. En ninguna
se usan interrupciones de GPIO (se ven más adelante en el curso).

## Arduino → ESP-IDF: equivalencias usadas en este port

| Arduino (`.ino`) | ESP-IDF (`main*.c`) |
|---|---|
| `pinMode(pin, OUTPUT)` | `gpio_reset_pin(pin); gpio_set_direction(pin, GPIO_MODE_OUTPUT);` |
| `pinMode(pin, INPUT_PULLUP / INPUT_PULLDOWN)` | `gpio_reset_pin(pin); gpio_set_direction(pin, GPIO_MODE_INPUT); gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY / GPIO_PULLDOWN_ONLY);` |
| `digitalRead(pin)` | `gpio_get_level(pin)` |
| `digitalWrite(pin, HIGH/LOW)` | `gpio_set_level(pin, 1/0)` |
| `HIGH` / `LOW` | `1` / `0` |
| `millis()` | `esp_timer_get_time() / 1000` (`esp_timer_get_time()` da µs en `int64_t`) |
| `delay(ms)` | `vTaskDelay(pdMS_TO_TICKS(ms))` (cede la CPU a FreeRTOS) |
| `analogReadResolution(12)` + `analogSetPinAttenuation(pin, ADC_11db)` | `adc_oneshot_new_unit()` + `adc_oneshot_config_channel()` con `ADC_BITWIDTH_12` y `ADC_ATTEN_DB_12` |
| `analogRead(pin)` (cuentas 0–4095) | `adc_oneshot_read(handle, canal, &raw)` |
| *(no existe: había que hacer `raw*3300/4095`)* | `adc_cali_create_scheme_curve_fitting()` + `adc_cali_raw_to_voltage(cali, raw, &mv)` → **mV calibrados** |
| `Serial.begin(115200)` | *(nada: la consola ya está a 115200)* |
| `Serial.printf(...)` / `Serial.println(...)` | `printf(...)` |
| `setup()` + `loop()` | `void app_main(void)` con `while (true) { ... vTaskDelay(...); }` |
| número de pin (`uint8_t`) | número de pin (`int`; en ESP-IDF también existe `GPIO_NUM_x`) |

## Compilación de referencia

Compilado con PlatformIO (`platform = espressif32 6.12.0`, **ESP-IDF 5.5.0**,
`framework = espidf`) sobre el proyecto base del curso: **SUCCESS** en las dos
versiones, sin *warnings* del compilador en `main.c`.
