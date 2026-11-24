# BLESerial

**Serial por Bluetooth Low Energy para ESP32**: lo que imprimes con `BLESerial.print()` llega al
celular y lo que el celular escribe lo lees con `BLESerial.read()`, igual que con `Serial`, pero
sin cable.

Usa el perfil **Nordic UART Service (NUS)** sin modificaciones, así que funciona con cualquier app
que hable NUS: la app del curso, **nRF Connect** o **Serial Bluetooth Terminal**.

> **Aviso: todavía no se ha probado en radio.** La biblioteca compila sin warnings en las
> combinaciones marcadas abajo, pero ninguna se ha probado aún con una placa y un celular reales.
> Si la pruebas, reporta lo que encuentres.

Versión 1.0.0 · Licencia MIT · Repositorio:
[esmarr58/embebidos-2026b](https://github.com/esmarr58/embebidos-2026b), carpeta `bibliotecas/BLESerial/`.

---

## Versiones compatibles

| Framework | Core / ESP-IDF | Plataforma PlatformIO | Chip | Estado |
|---|---|---|---|---|
| Arduino | arduino-esp32 **2.0.17** (IDF 4.4) | `espressif32@6.12.0` | ESP32-S3 | ✅ Compila (Eco, Plotter, Comandos) |
| Arduino | arduino-esp32 2.0.17 | `espressif32@6.12.0` | ESP32 | ✅ Compila (Eco) |
| Arduino | arduino-esp32 2.0.17 | `espressif32@6.12.0` | ESP32-C3 | ✅ Compila (Eco) |
| Arduino | arduino-esp32 **3.x** (3.3.11, IDF 5.5) | pioarduino `55.03.311` | ESP32-S3 | ⚠️ No probado: el código ya está adaptado al 3.x, pero no se compiló |
| Arduino | arduino-esp32 3.x | — | ESP32-C6 | ⚠️ No probado (el C6 solo existe en el core 3.x) |
| ESP-IDF | **5.5.0** | `espressif32@6.12.0` | ESP32-S3 | ✅ Compila (eco, plotter, comandos) |
| ESP-IDF | 5.5.0 | `espressif32@6.12.0` | ESP32 | ✅ Compila (eco) |
| ESP-IDF | 5.5.0 | `espressif32@6.12.0` | ESP32-C3 | ✅ Compila (eco) |
| ESP-IDF | 5.3 / 5.4 | pioarduino | todos | ⚠️ No probado |
| ESP-IDF | 5.x | — | ESP32-C6 | ⚠️ No probado |
| — | — | — | ESP32-S2 | ❌ No es compatible: el S2 no tiene Bluetooth |

✅ = compila sin warnings de la biblioteca; **ningún renglón está probado en radio**.
⚠️ = puede funcionar, pero nadie lo ha compilado todavía.

---

## Qué hace

- Servicio NUS `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`:
  - **RX** `6E400002-…` (Write y Write Without Response): del celular a la ESP32.
  - **TX** `6E400003-…` (Notify con CCCD 0x2902): de la ESP32 al celular.
- El UUID del servicio va en el advertising y el nombre en el scan response. Nombre por defecto
  `ESP32-Serial` (máximo 29 caracteres). Sin emparejamiento ni cifrado.
- Acepta MTU hasta 517. Cada notificación lleva como máximo MTU−3 bytes; con MTU 23 sale en
  trozos de 20. Cada escritura RX puede traer hasta MTU−3 bytes (512 o más si el celular negocia
  517) y entra completa al búfer; si no cabe, se descarta entera y se cuenta. Nunca se trunca.
- **Es un flujo de bytes, no de mensajes**: una línea puede llegar partida en varios paquetes y
  un paquete puede traer varias líneas. Para trabajar por líneas usa `readStringUntil('\n')`,
  `ble_serial_read_line()` o el callback de línea.
- **`print()` nunca bloquea `loop()`**: copia a un búfer circular (2048 bytes) y una tarea de la
  biblioteca hace el envío. Si el búfer se llena se tiran los bytes **más antiguos** y se cuenta.
- Sin conexión, lo que imprimes se descarta (o se guarda, si lo activas). Cuando el celular se
  desconecta, la ESP32 vuelve a anunciarse sola.
- Se puede llamar desde cualquier tarea de FreeRTOS, **pero no desde una interrupción (ISR)**.

## API resumida

### Arduino (objeto `BLESerial`, hereda de `Stream`)

| Método | Qué hace |
|---|---|
| `begin(nombre = "ESP32-Serial")` | inicia BLE y el advertising; devuelve `bool` |
| `print`, `println`, `printf`, `write` | escriben (vienen de `Stream`/`Print`) |
| `available`, `read`, `peek`, `readStringUntil`, `readBytes` | leen (vienen de `Stream`) |
| `flush()` | espera a que salga el búfer TX (máx. 1 s, solo si hay alguien suscrito) |
| `connected()`, `subscribed()`, `mtu()`, `droppedBytes()` | estado |
| `setTxTimeout(ms)`, `setBufferWhenDisconnected(bool)` | ajustes de envío |
| `setRxCallback(cb, ctx)`, `redirectLog(bool)` | callback por línea; `ESP_LOGx` también por BLE |

### ESP-IDF (C, `#include "ble_serial.h"`)

| Función | Devuelve |
|---|---|
| `ble_serial_init(nombre)` | `esp_err_t` (`NULL` = nombre por defecto) |
| `ble_serial_printf(fmt, ...)` | `int`, bytes encolados |
| `ble_serial_write(buf, len)` | `size_t`, bytes encolados |
| `ble_serial_available()` | `size_t` |
| `ble_serial_read(buf, maxlen, timeout_ticks)` | `size_t` leídos |
| `ble_serial_read_line(buf, maxlen, timeout_ticks)` | `int`, longitud de la línea, o −1 |
| `ble_serial_connected()` | `bool` |
| `ble_serial_set_rx_callback(cb, ctx)` | callback por línea completa (corre en la tarea BLE) |
| `ble_serial_redirect_log(true)` | `ESP_LOGx` también sale por BLE |

Además: `ble_serial_peek`, `ble_serial_subscribed`, `ble_serial_mtu`, `ble_serial_dropped`,
`ble_serial_rx_dropped`, `ble_serial_tx_free`, `ble_serial_set_tx_timeout`,
`ble_serial_set_buffer_when_disconnected` y `ble_serial_flush`.

En C++ con ESP-IDF, `#include "BLESerial.h"` te da un objeto `BLESerial` con los mismos nombres
que en Arduino (`readStringUntil` devuelve `std::string`).

---

## Instalación

La biblioteca vive en una **subcarpeta** del repositorio (`bibliotecas/BLESerial/`), y eso
determina cómo se instala en cada caso.

### A. Arduino IDE

1. Descarga [`bibliotecas/BLESerial.zip`](https://github.com/esmarr58/embebidos-2026b/raw/main/bibliotecas/BLESerial.zip)
   del repositorio.
2. En el Arduino IDE: **Programa → Incluir biblioteca → Añadir biblioteca .ZIP…** y elige el ZIP.

   *O bien* copia la carpeta `BLESerial` completa a `Documentos/Arduino/libraries/`.
3. Instala el core **esp32 de Espressif** (Gestor de tarjetas). La versión probada es la
   **2.0.17**. El IDE instala por defecto la 3.x: el código ya está adaptado, pero esa versión
   todavía no se ha compilado (ver la tabla).
4. Placa: por ejemplo **ESP32S3 Dev Module**. Ejemplos en **Archivo → Ejemplos → BLESerial → arduino**.

### B. PlatformIO

PlatformIO **no puede instalar una biblioteca desde una subcarpeta de un repositorio git**, así
que la URL del repositorio no sirve en `lib_deps`. Tienes dos opciones:

- Copia la carpeta `BLESerial` a la carpeta `lib/` de tu proyecto (no hace falta `lib_deps`), o
- enlázala desde donde la tengas:

```ini
[env:esp32s3]
platform = espressif32@6.12.0
board = esp32-s3-devkitc-1
framework = arduino          ; o espidf
monitor_speed = 115200
lib_deps = symlink://C:/ruta/a/BLESerial
```

Con `framework = espidf` también necesitas el `sdkconfig.defaults` de la sección C. En este modo
PlatformIO compila la biblioteca por su cuenta y **no lee su `Kconfig`**: los ajustes se cambian
con `build_flags`, por ejemplo `-DCONFIG_BLE_SERIAL_TX_BUFFER_SIZE=4096` (en Arduino también).

### C. ESP-IDF (idf.py)

**Opción 1: component manager.** Crea o edita `main/idf_component.yml` de tu proyecto:

```yaml
dependencies:
  bleserial:
    git: https://github.com/esmarr58/embebidos-2026b.git
    path: bibliotecas/BLESerial   # carpeta del componente dentro del repositorio
    # version: main               # opcional: rama, etiqueta o commit
```

`idf.py build` lo descarga a `managed_components/`. Con esto `main` ya depende de él; si
`main/CMakeLists.txt` lleva `REQUIRES`, agrega `bleserial` (el nombre de la clave).

**Opción 2: carpeta local.** Copia `BLESerial` a `components/BLESerial` de tu proyecto, o en el
`CMakeLists.txt` raíz agrega, **antes** del `include(...project.cmake)`:

```cmake
list(APPEND EXTRA_COMPONENT_DIRS "ruta/a/BLESerial")
```

Con esta opción el componente se llama `BLESerial`
(`idf_component_register(SRCS "main.c" REQUIRES BLESerial)`). Los ejemplos de
`examples/espidf/` están armados así.

**En los dos casos**, el proyecto necesita en `sdkconfig.defaults`:

```
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
# CONFIG_BT_BLUEDROID_ENABLED is not set
# Recomendado: si no, NimBLE escribe una línea de log por cada notificación
CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y
```

Si ya tenías un `sdkconfig` generado, bórralo (o usa `idf.py menuconfig`) para que se apliquen.
Las opciones de la biblioteca aparecen en **menuconfig → BLESerial (puerto serie sobre BLE)**.

---

## Ejemplo mínimo

### Arduino

```cpp
#include <BLESerial.h>

void setup() {
  BLESerial.begin("MiESP32");
}

void loop() {
  BLESerial.printf("temp:%.1f,hum:%d\n", 23.5, 48);   // una línea de datos para el plotter
  if (BLESerial.available()) {
    String cmd = BLESerial.readStringUntil('\n');
    cmd.trim();
    BLESerial.printf("recibi: %s\n", cmd.c_str());
  }
  delay(500);
}
```

### ESP-IDF

```c
#include "ble_serial.h"

void app_main(void)
{
    ESP_ERROR_CHECK(ble_serial_init("MiESP32"));
    char linea[128];
    for (;;) {
        ble_serial_printf("temp:%.1f\n", 23.5);
        if (ble_serial_read_line(linea, sizeof(linea), pdMS_TO_TICKS(500)) >= 0) {
            ble_serial_printf("recibi: %s\n", linea);
        }
    }
}
```

## Cómo probarlo con el celular

**nRF Connect** (Android/iOS):
1. Pestaña *Scanner*: busca `MiESP32` (o `ESP32-Serial`) y toca **Connect**.
2. Abre *Nordic UART Service*. En **TX** (`…0003`) toca el ícono de flechas para activar las
   notificaciones: ahí aparece lo que imprime la ESP32.
3. En **RX** (`…0002`) toca la flecha hacia arriba, elige *UTF-8* o *Text*, escribe `hola` y envía.
   Ojo: nRF Connect no agrega `\n` por su cuenta; si el programa espera líneas, termina el texto
   con un salto de línea.

**Serial Bluetooth Terminal** (Android):
1. Menú → *Devices* → pestaña *Bluetooth LE* → *Scan* → elige tu ESP32.
2. La app reconoce NUS sola. Escribe y envía; en *Settings → Send → Newline* elige `LF` (`\n`).

## Ejemplos incluidos

| Arduino | ESP-IDF | Qué hace |
|---|---|---|
| `Eco` | `eco` (en C++) | devuelve lo recibido |
| `Plotter` | `plotter` | cada 100 ms envía `seno:<v>,rampa:<v>` (formato del Serial Plotter) |
| `Comandos` | `comandos` | entiende `led=0`, `led=1` y `periodo=<ms>`; contesta `ok …` o `error …` |

## Ajustes

| Opción (Kconfig o `-D`) | Valor por defecto |
|---|---|
| `CONFIG_BLE_SERIAL_DEFAULT_NAME` | `"ESP32-Serial"` |
| `CONFIG_BLE_SERIAL_TX_BUFFER_SIZE` | 2048 |
| `CONFIG_BLE_SERIAL_RX_BUFFER_SIZE` | 2048 (caben 4 escrituras de 512) |
| `CONFIG_BLE_SERIAL_MAX_LINE` (callback) | 256 |
| `CONFIG_BLE_SERIAL_TX_TIMEOUT_MS` | 0 (nunca espera) |
| `CONFIG_BLE_SERIAL_BUFFER_WHILE_DISCONNECTED` | no |
| `CONFIG_BLE_SERIAL_LOCAL_MTU` | 517 |
| `CONFIG_BLE_SERIAL_TASK_STACK` / `_PRIORITY` | 4096 / 5 |

## Cómo está hecha

- `src/ble_serial_core.c`: núcleo común en C (búferes circulares, mutex, tarea de envío, troceo
  por MTU−3, reensamblado por línea). Es el mismo para todos los frameworks.
- `src/ble_serial_bluedroid.cpp`: backend para Arduino con la biblioteca BLE del core (Bluedroid),
  sin dependencias externas. Funciona con el core 2.x y con el 3.x.
- `src/ble_serial_nimble.c`: backend para ESP-IDF con NimBLE. También se usaría en un core
  Arduino compilado con NimBLE en lugar de Bluedroid.
- `src/BLESerial.h` / `BLESerial.cpp`: el objeto `BLESerial`.

## Limitaciones conocidas

- Un solo celular conectado a la vez.
- No hay `end()` (desinicialización) en la versión 1.
- `println()` de Arduino manda `\r\n` en dos escrituras. Si varias tareas imprimen al mismo
  tiempo, usa `printf("...\n")` para que cada línea salga completa.
- Con `redirectLog`, los mensajes que se generan dentro de la propia tarea de envío no se
  reenvían (así se evita un ciclo infinito con el log del stack).
