// Proyecto final I7267 2026B - Robot movil controlado por WebSockets
// Nombre: ______________________  Codigo: __________  Fecha: __________
//
// ESP32-S3-DevKitC-1 + L298N + 2 motores TT + 2 pilas 18650.
// PlatformIO espressif32@6.12.0 -> ESP-IDF 5.5.0 (sin Arduino).
//
// El robot crea su PROPIA red WiFi (SoftAP), no necesita internet:
//   SSID      ROBOT-S3-xxxx   (xxxx = ultimos 2 bytes de la MAC, se imprime al arrancar)
//   Clave     embebidos2026
//   Servidor  ws://192.168.4.1/ws   (cualquier app cliente de WebSocket)
//
// Comandos de texto (uno por mensaje, mayusculas o minusculas):
//   adelante [ms]   atras [ms]   izq [ms]   der [ms]   alto
//   vel <0-100>     prueba a|b   estado     ayuda
//   Atajos: w=adelante s=atras a=izq d=der x=alto
//   Con [ms] (50-10000) el robot se mueve ese tiempo y se detiene solo.
//   Tambien acepta el JSON del robot 2025A / app Qt de Interfaces:
//     {"tipo":"mover","accion":"avanzar","ms":600}  y  {"tipo":"latido"}
//
// Seguridad: el robot se DETIENE si se cierra el WebSocket o si el telefono
// se sale de la red del robot.
//
// Pines (los mismos IN1-IN4 del robot de Diseno de Interfaces):
//   Motor A (izquierdo): IN1=GPIO11 IN2=GPIO12 ENA=GPIO9  (PWM MCPWM0A)
//   Motor B (derecho)  : IN3=GPIO13 IN4=GPIO14 ENB=GPIO10 (PWM MCPWM0B)
//   Quita los jumpers ENA y ENB del L298N: la velocidad la da el PWM.

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/mcpwm.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "ROBOT";

// ---------------------------------------------------------------- Pines
#define PIN_IN1   GPIO_NUM_11
#define PIN_IN2   GPIO_NUM_12
#define PIN_IN3   GPIO_NUM_13
#define PIN_IN4   GPIO_NUM_14
#define PIN_ENA   9
#define PIN_ENB   10

#define FREC_PWM      1000     // Hz: el L298N trabaja bien a 1 kHz
#define VEL_INICIAL   60       // % de PWM al arrancar
#define MS_MIN        50
#define MS_MAX        10000
#define MAX_CLIENTES  4

#define AP_CLAVE      "embebidos2026"
#define AP_CANAL      1

// ---------------------------------------------------------------- Estado
typedef enum { MOV_ALTO = 0, MOV_ADELANTE, MOV_ATRAS, MOV_IZQ, MOV_DER } movimiento_t;
static const char *NOMBRE_MOV[] = { "alto", "adelante", "atras", "izq", "der" };

static SemaphoreHandle_t candado;          // protege el estado de motores
static movimiento_t mov_actual = MOV_ALTO;
static int vel_pct = VEL_INICIAL;
static esp_timer_handle_t timer_paro;      // paro automatico tras [ms]
static int clientes_ws[MAX_CLIENTES];      // descriptores de sockets WebSocket
static int n_clientes = 0;
static uint32_t n_comandos = 0;
static char ssid[32];

// ---------------------------------------------------------------- Motores
static void pwm_init(void)
{
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, PIN_ENA);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, PIN_ENB);
    mcpwm_config_t cfg = {
        .frequency = FREC_PWM,
        .cmpr_a = 0,                       // arranca en 0 %: motores quietos
        .cmpr_b = 0,
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,    // activa en alto
    };
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &cfg);
}

static void gpio_dir_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_IN1) | (1ULL << PIN_IN2) | (1ULL << PIN_IN3) | (1ULL << PIN_IN4),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_IN1, 0); gpio_set_level(PIN_IN2, 0);
    gpio_set_level(PIN_IN3, 0); gpio_set_level(PIN_IN4, 0);
}

// sentido: +1 adelante, -1 atras, 0 libre (IN1=IN2=0)
static void motor_a(int sentido) { gpio_set_level(PIN_IN1, sentido > 0); gpio_set_level(PIN_IN2, sentido < 0); }
static void motor_b(int sentido) { gpio_set_level(PIN_IN3, sentido > 0); gpio_set_level(PIN_IN4, sentido < 0); }

static void pwm_poner(int pct_a, int pct_b)
{
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, (float)pct_a);
    mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, (float)pct_b);
}

// Aplica un movimiento. Llamar con el candado tomado.
static void aplicar(movimiento_t m)
{
    if (m != mov_actual && mov_actual != MOV_ALTO && m != MOV_ALTO) {
        // cambio de sentido: suelta los motores un instante para no "golpear" la caja de engranes
        pwm_poner(0, 0); motor_a(0); motor_b(0);
        vTaskDelay(pdMS_TO_TICKS(40));
    }
    switch (m) {
    case MOV_ADELANTE: motor_a(+1); motor_b(+1); break;
    case MOV_ATRAS:    motor_a(-1); motor_b(-1); break;
    case MOV_IZQ:      motor_a(-1); motor_b(+1); break;   // gira sobre su eje
    case MOV_DER:      motor_a(+1); motor_b(-1); break;
    default:           motor_a(0);  motor_b(0);  break;
    }
    int v = (m == MOV_ALTO) ? 0 : vel_pct;
    // TODO (tu extra): rampa de aceleracion, ajuste fino por motor si el robot se desvia, etc.
    pwm_poner(v, v);
    mov_actual = m;
}

static void detener(const char *motivo)
{
    esp_timer_stop(timer_paro);            // no pasa nada si no estaba corriendo
    xSemaphoreTake(candado, portMAX_DELAY);
    bool estaba = (mov_actual != MOV_ALTO);
    aplicar(MOV_ALTO);
    xSemaphoreGive(candado);
    if (estaba) ESP_LOGI(TAG, "ALTO (%s)", motivo);
}

static void cb_timer_paro(void *arg) { detener("se cumplio el tiempo"); }

// ---------------------------------------------------------------- Clientes
static void cliente_agregar(int fd)
{
    xSemaphoreTake(candado, portMAX_DELAY);
    bool ya = false;
    for (int i = 0; i < n_clientes; i++) if (clientes_ws[i] == fd) ya = true;
    if (!ya && n_clientes < MAX_CLIENTES) clientes_ws[n_clientes++] = fd;
    xSemaphoreGive(candado);
}

// Devuelve true si fd era un cliente WebSocket
static bool cliente_quitar(int fd)
{
    bool era = false;
    xSemaphoreTake(candado, portMAX_DELAY);
    for (int i = 0; i < n_clientes; i++) {
        if (clientes_ws[i] == fd) { clientes_ws[i] = clientes_ws[--n_clientes]; era = true; break; }
    }
    xSemaphoreGive(candado);
    return era;
}

// ---------------------------------------------------------------- Comandos
static const char *AYUDA =
    "Comandos: adelante [ms] | atras [ms] | izq [ms] | der [ms] | alto | vel <0-100> | "
    "prueba a | prueba b | estado | ayuda. Atajos: w s a d x. "
    "Ejemplo: adelante 800 = avanza 0.8 s y se detiene.";

static movimiento_t a_movimiento(const char *p)
{
    if (!strcasecmp(p, "adelante") || !strcasecmp(p, "w")) return MOV_ADELANTE;
    if (!strcasecmp(p, "atras") || !strcasecmp(p, "s"))    return MOV_ATRAS;
    if (!strcasecmp(p, "izq") || !strcasecmp(p, "a"))      return MOV_IZQ;
    if (!strcasecmp(p, "der") || !strcasecmp(p, "d"))      return MOV_DER;
    return (movimiento_t)-1;
}

static void procesar_texto(char *linea, char *resp, size_t n);

// Compatibilidad con el protocolo JSON del robot de 2025A y de la app de Qt de
// Diseno de Interfaces:  {"tipo":"mover","accion":"avanzar|retroceder|izquierda|derecha|parar","ms":600}
//                        {"tipo":"latido"}
// Se traduce al comando de texto equivalente y se responde en JSON.
static void procesar_json(const char *txt, char *resp, size_t n)
{
    cJSON *root = cJSON_Parse(txt);
    const char *tipo = root ? cJSON_GetStringValue(cJSON_GetObjectItem(root, "tipo")) : NULL;
    if (!tipo) {
        snprintf(resp, n, "{\"tipo\":\"error\",\"mensaje\":\"JSON sin campo tipo\"}");
    } else if (!strcmp(tipo, "latido")) {
        snprintf(resp, n, "{\"tipo\":\"latido_ok\",\"tiempo_ms\":%llu}",
                 (unsigned long long)(esp_timer_get_time() / 1000));
    } else if (!strcmp(tipo, "mover")) {
        const char *acc = cJSON_GetStringValue(cJSON_GetObjectItem(root, "accion"));
        cJSON *jms = cJSON_GetObjectItem(root, "ms");
        int ms = cJSON_IsNumber(jms) ? (int)cJSON_GetNumberValue(jms) : 600;
        if (ms < MS_MIN) ms = 600;
        if (ms > MS_MAX) ms = MS_MAX;
        const char *cmd = "adelante";                 // como en 2025A: sin accion = avanzar
        if (acc && !strcmp(acc, "retroceder")) cmd = "atras";
        else if (acc && !strcmp(acc, "izquierda")) cmd = "izq";
        else if (acc && !strcmp(acc, "derecha")) cmd = "der";
        else if (acc && !strcmp(acc, "parar")) cmd = "alto";
        char linea[32], txt_resp[200];
        if (!strcmp(cmd, "alto")) snprintf(linea, sizeof linea, "alto");
        else snprintf(linea, sizeof linea, "%s %d", cmd, ms);
        procesar_texto(linea, txt_resp, sizeof txt_resp);
        bool ok = !strncmp(txt_resp, "ok", 2);
        snprintf(resp, n, "{\"tipo\":\"%s\",\"accion\":\"%s\",\"ms\":%d}",
                 ok ? "mover_ack" : "mover_err", acc ? acc : "avanzar", ms);
    } else {
        // TODO (tu extra): otros tipos JSON (telemetria, bateria, rgb, ...)
        snprintf(resp, n, "{\"tipo\":\"error\",\"mensaje\":\"tipo desconocido\"}");
    }
    cJSON_Delete(root);
}

// Interpreta un mensaje (texto o JSON) y escribe la respuesta en resp
static void procesar(char *linea, char *resp, size_t n)
{
    const char *p = linea;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '{') { procesar_json(p, resp, n); ESP_LOGI(TAG, "%s", resp); return; }
    procesar_texto(linea, resp, n);
}

// Interpreta una linea de texto y escribe la respuesta en resp
static void procesar_texto(char *linea, char *resp, size_t n)
{
    // recorta espacios y saltos de linea que algunas apps agregan
    while (*linea && isspace((unsigned char)*linea)) linea++;
    char *fin = linea + strlen(linea);
    while (fin > linea && isspace((unsigned char)fin[-1])) *--fin = '\0';

    char *guardar = NULL;
    char *cmd = strtok_r(linea, " ", &guardar);
    char *arg = strtok_r(NULL, " ", &guardar);
    n_comandos++;
    if (!cmd) { snprintf(resp, n, "error: mensaje vacio. Escribe ayuda"); return; }

    movimiento_t m = a_movimiento(cmd);
    if ((int)m >= 0) {
        int ms = 0;
        if (arg) {
            ms = atoi(arg);
            if (ms < MS_MIN || ms > MS_MAX) {
                snprintf(resp, n, "error: el tiempo debe ser de %d a %d ms (recibi \"%s\")", MS_MIN, MS_MAX, arg);
                return;
            }
        }
        esp_timer_stop(timer_paro);
        xSemaphoreTake(candado, portMAX_DELAY);
        aplicar(m);
        int v = vel_pct;
        xSemaphoreGive(candado);
        if (ms) {
            esp_timer_start_once(timer_paro, (uint64_t)ms * 1000);
            snprintf(resp, n, "ok: %s %d ms a vel %d%%", NOMBRE_MOV[m], ms, v);
        } else {
            snprintf(resp, n, "ok: %s a vel %d%% (envia alto para detener)", NOMBRE_MOV[m], v);
        }
        ESP_LOGI(TAG, "%s", resp);
        return;
    }
    if (!strcasecmp(cmd, "alto") || !strcasecmp(cmd, "x") || !strcasecmp(cmd, "stop")) {
        detener("comando alto");
        snprintf(resp, n, "ok: alto");
        return;
    }
    if (!strcasecmp(cmd, "vel")) {
        char *e = NULL;
        long v = arg ? strtol(arg, &e, 10) : -1;
        if (!arg || *e || v < 0 || v > 100) { snprintf(resp, n, "error: usa vel <0-100>, por ejemplo vel 70"); return; }
        xSemaphoreTake(candado, portMAX_DELAY);
        vel_pct = (int)v;
        if (mov_actual != MOV_ALTO) pwm_poner(vel_pct, vel_pct);   // cambia al vuelo
        xSemaphoreGive(candado);
        snprintf(resp, n, "ok: vel %ld%%", v);
        ESP_LOGI(TAG, "%s", resp);
        return;
    }
    if (!strcasecmp(cmd, "prueba") && arg && (!strcasecmp(arg, "a") || !strcasecmp(arg, "b"))) {
        // Mueve UN solo motor hacia adelante 1 s: sirve para revisar el cableado
        bool es_a = !strcasecmp(arg, "a");
        esp_timer_stop(timer_paro);
        xSemaphoreTake(candado, portMAX_DELAY);
        aplicar(MOV_ALTO);
        if (es_a) { motor_a(+1); pwm_poner(vel_pct, 0); }
        else      { motor_b(+1); pwm_poner(0, vel_pct); }
        mov_actual = MOV_ADELANTE;      // para que alto/desconexion lo detengan
        xSemaphoreGive(candado);
        esp_timer_start_once(timer_paro, 1000 * 1000);
        snprintf(resp, n, "ok: prueba motor %s (%s) 1 s hacia adelante. Si gira al reves, invierte sus 2 cables en el L298N",
                 es_a ? "A" : "B", es_a ? "izquierdo, OUT1/OUT2" : "derecho, OUT3/OUT4");
        ESP_LOGI(TAG, "%s", resp);
        return;
    }
    if (!strcasecmp(cmd, "estado")) {
        xSemaphoreTake(candado, portMAX_DELAY);
        snprintf(resp, n, "estado: red=%s mov=%s vel=%d%% clientes=%d comandos=%lu activo=%llu s",
                 ssid, NOMBRE_MOV[mov_actual], vel_pct, n_clientes, (unsigned long)n_comandos,
                 (unsigned long long)(esp_timer_get_time() / 1000000));
        xSemaphoreGive(candado);
        // TODO (tu extra): agrega aqui tu dato, p. ej. bateria=7.6V leida por ADC con divisor
        return;
    }
    if (!strcasecmp(cmd, "ayuda") || !strcasecmp(cmd, "?")) { snprintf(resp, n, "%s", AYUDA); return; }

    // TODO (tu extra): agrega aqui tus comandos nuevos (coreografia, luces, modo autonomo, ...)
    snprintf(resp, n, "error: no entiendo \"%s\". Escribe ayuda", cmd);
}

// ---------------------------------------------------------------- WebSocket
static esp_err_t enviar_texto(httpd_req_t *req, const char *txt)
{
    httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)txt, .len = strlen(txt) };
    return httpd_ws_send_frame(req, &f);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if (req->method == HTTP_GET) {          // saludo (handshake) del WebSocket
        cliente_agregar(fd);
        ESP_LOGI(TAG, "cliente conectado (socket %d)", fd);
        return ESP_OK;
    }
    httpd_ws_frame_t f = { 0 };
    esp_err_t r = httpd_ws_recv_frame(req, &f, 0);    // primero solo la longitud
    if (r != ESP_OK) return r;
    if (f.type == HTTPD_WS_TYPE_CLOSE) {
        if (cliente_quitar(fd)) detener("el cliente cerro el WebSocket");
        return ESP_OK;
    }
    if (f.type != HTTPD_WS_TYPE_TEXT || f.len == 0) return ESP_OK;
    if (f.len > 128) return enviar_texto(req, "error: mensaje demasiado largo (max 128)");

    char buf[129];
    f.payload = (uint8_t *)buf;
    r = httpd_ws_recv_frame(req, &f, f.len);
    if (r != ESP_OK) return r;
    buf[f.len] = '\0';
    cliente_agregar(fd);                    // por si se reinicio la cuenta

    char resp[256];
    procesar(buf, resp, sizeof resp);
    return enviar_texto(req, resp);
}

// Pagina de prueba en http://192.168.4.1/ (solo texto, para confirmar la red)
static esp_err_t raiz_handler(httpd_req_t *req)
{
    char html[400];
    snprintf(html, sizeof html,
             "Robot %s listo.\nConecta tu app de WebSocket a ws://192.168.4.1/ws\n%s\n", ssid, AYUDA);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, html);
}

// Se llama al cerrarse CUALQUIER socket del servidor
static void al_cerrar_socket(httpd_handle_t hd, int fd)
{
    if (cliente_quitar(fd)) {
        ESP_LOGW(TAG, "cliente desconectado (socket %d)", fd);
        detener("se perdio la conexion");
    }
    close(fd);
}

static void servidor_iniciar(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.close_fn = al_cerrar_socket;
    cfg.lru_purge_enable = true;
    httpd_handle_t srv = NULL;
    ESP_ERROR_CHECK(httpd_start(&srv, &cfg));
    httpd_uri_t ws = { .uri = "/ws", .method = HTTP_GET, .handler = ws_handler, .is_websocket = true };
    httpd_uri_t raiz = { .uri = "/", .method = HTTP_GET, .handler = raiz_handler };
    httpd_register_uri_handler(srv, &ws);
    httpd_register_uri_handler(srv, &raiz);
    ESP_LOGI(TAG, "servidor listo: ws://192.168.4.1/ws");
}

// ---------------------------------------------------------------- WiFi (SoftAP)
static void wifi_evento(void *arg, esp_event_base_t base, int32_t id, void *datos)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "un telefono/PC se unio a %s", ssid);
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGW(TAG, "un telefono/PC salio de la red");
        detener("el cliente salio de la red WiFi");
    }
}

static void wifi_ap_iniciar(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&ic));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evento, NULL));

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof ssid, "ROBOT-S3-%02X%02X", mac[4], mac[5]);

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.ap.ssid, ssid, sizeof wc.ap.ssid);
    wc.ap.ssid_len = strlen(ssid);
    strlcpy((char *)wc.ap.password, AP_CLAVE, sizeof wc.ap.password);
    wc.ap.channel = AP_CANAL;
    wc.ap.max_connection = MAX_CLIENTES;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "red WiFi: %s  clave: %s  IP: 192.168.4.1", ssid, AP_CLAVE);
}

// ---------------------------------------------------------------- Extra
// TODO (tu extra): si tu extra necesita trabajo periodico (leer bateria por ADC,
// sensor ultrasonico para frenar, parpadear un LED de estado), hazlo en esta tarea.
static void tarea_extra(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    esp_err_t r = nvs_flash_init();         // el WiFi guarda su calibracion en NVS
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        r = nvs_flash_init();
    }
    ESP_ERROR_CHECK(r);

    candado = xSemaphoreCreateMutex();
    gpio_dir_init();
    pwm_init();
    const esp_timer_create_args_t ta = { .callback = cb_timer_paro, .name = "paro" };
    ESP_ERROR_CHECK(esp_timer_create(&ta, &timer_paro));

    wifi_ap_iniciar();
    servidor_iniciar();
    xTaskCreate(tarea_extra, "extra", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "PROYECTO FINAL | ROBOT WS | %s | vel inicial %d%% | motores en ALTO", ssid, VEL_INICIAL);
    ESP_LOGI(TAG, "%s", AYUDA);
}
