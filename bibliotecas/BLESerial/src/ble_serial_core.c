/*
 * ble_serial_core.c - nucleo comun de BLESerial (C puro, igual en Arduino y ESP-IDF).
 *
 *  - Bufer TX circular: si se llena se descartan los bytes MAS ANTIGUOS y se cuenta.
 *  - Bufer RX circular: una escritura BLE que no cabe completa se descarta entera
 *    (nunca se trunca) y se cuenta. Con el defecto de 2048 caben 4 escrituras de 512.
 *  - Un mutex protege todo el estado compartido; nadie lo retiene durante un envio.
 *  - Una tarea propia saca trozos de MTU-3 bytes y se los pasa al backend. Quien llama a
 *    print/write solo copia al bufer y despierta a la tarea: nunca espera a la radio.
 *  - Reensamblado por '\n' para el callback de linea y para ble_serial_read_line().
 *
 * Licencia MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "ble_serial.h"
#include "ble_serial_core.h"

static const char *TAG = "ble_serial";

/* ---- Bufer circular de bytes ------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t head; /* indice del byte mas antiguo */
    size_t len;  /* bytes guardados */
} ring_t;

static void ring_clear(ring_t *r)
{
    r->head = 0;
    r->len = 0;
}

static size_t ring_free(const ring_t *r)
{
    return r->cap - r->len;
}

/* Copia n bytes al final, sin revisar espacio (el llamador ya lo garantizo). */
static void ring_put(ring_t *r, const uint8_t *src, size_t n)
{
    size_t tail = (r->head + r->len) % r->cap;
    size_t first = r->cap - tail;
    if (first > n) {
        first = n;
    }
    memcpy(r->buf + tail, src, first);
    memcpy(r->buf, src + first, n - first);
    r->len += n;
}

/* Saca hasta n bytes del inicio. out puede ser NULL para solo descartarlos. */
static size_t ring_get(ring_t *r, uint8_t *out, size_t n)
{
    if (n > r->len) {
        n = r->len;
    }
    if (out != NULL) {
        size_t first = r->cap - r->head;
        if (first > n) {
            first = n;
        }
        memcpy(out, r->buf + r->head, first);
        memcpy(out + first, r->buf, n - first);
    }
    r->head = (r->head + n) % r->cap;
    r->len -= n;
    return n;
}

/* Escribe descartando lo mas antiguo si no cabe. Devuelve los bytes descartados. */
static size_t ring_put_overwrite(ring_t *r, const uint8_t *src, size_t n)
{
    size_t dropped = 0;
    if (n > r->cap) {
        /* Solo cabe la cola de los datos nuevos; lo previo y el inicio se pierden. */
        dropped = r->len + (n - r->cap);
        src += n - r->cap;
        n = r->cap;
        ring_clear(r);
    } else if (n > ring_free(r)) {
        dropped = n - ring_free(r);
        ring_get(r, NULL, dropped);
    }
    ring_put(r, src, n);
    return dropped;
}

static uint8_t ring_at(const ring_t *r, size_t i)
{
    return r->buf[(r->head + i) % r->cap];
}

/* Posicion (+1) del primer byte igual a c dentro de los primeros limit, o 0. */
static size_t ring_find(const ring_t *r, uint8_t c, size_t limit)
{
    size_t n = r->len < limit ? r->len : limit;
    for (size_t i = 0; i < n; i++) {
        if (ring_at(r, i) == c) {
            return i + 1;
        }
    }
    return 0;
}

/* ---- Estado ----------------------------------------------------------------------- */

typedef struct {
    SemaphoreHandle_t lock;    /* mutex del estado */
    SemaphoreHandle_t rx_sem;  /* se da cada que llegan datos */
    SemaphoreHandle_t tx_space;/* se da cada que la tarea saca datos del bufer TX */
    TaskHandle_t task;

    ring_t tx;
    ring_t rx;

    /* Trozo que la tarea esta enviando (solo la tarea escribe en pend[]). */
    uint8_t pend[BSC_MAX_CHUNK];
    size_t pend_len;
    size_t pend_off;

    bool connected;
    bool subscribed;
    uint16_t mtu;
    bool keep_offline;
    uint32_t tx_timeout_ms;
    uint32_t tx_dropped;
    uint32_t rx_dropped;

    ble_serial_rx_cb_t rx_cb;
    void *rx_ctx;

    /* Reensamblado de lineas para el callback (solo lo toca la tarea BLE). */
    char line[CONFIG_BLE_SERIAL_MAX_LINE + 1];
    size_t line_len;
} bsc_state_t;

static bsc_state_t *s; /* NULL hasta ble_serial_init() */

static vprintf_like_t s_prev_vprintf;
static bool s_log_redirected;

static inline void lock(void)
{
    xSemaphoreTake(s->lock, portMAX_DELAY);
}

static inline void unlock(void)
{
    xSemaphoreGive(s->lock);
}

static inline bool in_isr(void)
{
    return xPortInIsrContext() != 0;
}

static TickType_t ms_to_ticks(uint32_t ms)
{
    TickType_t t = pdMS_TO_TICKS(ms);
    return (ms != 0 && t == 0) ? 1 : t;
}

static void wake_task(void)
{
    if (s != NULL && s->task != NULL) {
        xTaskNotifyGive(s->task);
    }
}

static size_t chunk_size(void)
{
    size_t c = (size_t)s->mtu - 3;
    if (c > BSC_MAX_CHUNK) {
        c = BSC_MAX_CHUNK;
    }
    return c;
}

/* ---- Tarea de envio --------------------------------------------------------------- */

static void tx_task(void *arg)
{
    (void)arg;
    for (;;) {
        size_t n = 0;
        size_t off = 0;

        lock();
        bool can_send = s->connected && s->subscribed;
        if (can_send) {
            if (s->pend_off >= s->pend_len) {
                s->pend_len = ring_get(&s->tx, s->pend, chunk_size());
                s->pend_off = 0;
                if (s->pend_len > 0) {
                    xSemaphoreGive(s->tx_space);
                }
            }
            n = s->pend_len - s->pend_off;
            if (n > chunk_size()) {
                n = chunk_size(); /* el MTU bajo tras reconectar: se trocea de nuevo */
            }
            off = s->pend_off;
        }
        unlock();

        if (n == 0) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        /* Sin mutex: el backend puede tardar y puede generar eventos. */
        bsc_send_result_t r = bsc_backend_send(s->pend + off, n);

        if (r == BSC_SEND_OK) {
            lock();
            s->pend_off = off + n;
            unlock();
        } else if (r == BSC_SEND_BUSY) {
            ulTaskNotifyTake(pdTRUE, ms_to_ticks(10));
        } else {
            /* Probablemente se acaba de desconectar; el evento corrige el estado. */
            ulTaskNotifyTake(pdTRUE, ms_to_ticks(10));
        }
    }
}

/* ---- Eventos del backend ---------------------------------------------------------- */

void bsc_on_connect(void)
{
    if (s == NULL) {
        return;
    }
    lock();
    s->connected = true;
    s->subscribed = false;
    s->mtu = BSC_MIN_MTU;
    s->line_len = 0;
    unlock();
    wake_task();
}

void bsc_on_disconnect(void)
{
    if (s == NULL) {
        return;
    }
    lock();
    s->connected = false;
    s->subscribed = false;
    s->mtu = BSC_MIN_MTU;
    s->line_len = 0;
    if (!s->keep_offline) {
        ring_clear(&s->tx);
        s->pend_len = 0;
        s->pend_off = 0;
    }
    unlock();
    xSemaphoreGive(s->tx_space); /* libera escritores que esperaban espacio */
    wake_task();
}

void bsc_on_mtu(uint16_t mtu)
{
    if (s == NULL) {
        return;
    }
    if (mtu < BSC_MIN_MTU) {
        mtu = BSC_MIN_MTU;
    }
    lock();
    s->mtu = mtu;
    unlock();
    wake_task();
}

void bsc_on_subscribe(bool notify_enabled)
{
    if (s == NULL) {
        return;
    }
    lock();
    s->subscribed = notify_enabled;
    unlock();
    wake_task();
}

void bsc_on_tx_ready(void)
{
    wake_task();
}

static void deliver_line(ble_serial_rx_cb_t cb, void *ctx)
{
    size_t n = s->line_len;
    if (n > 0 && s->line[n - 1] == '\r') {
        n--;
    }
    s->line[n] = '\0';
    cb(s->line, n, ctx);
    s->line_len = 0;
}

void bsc_on_rx(const uint8_t *data, size_t len)
{
    if (s == NULL || len == 0) {
        return;
    }
    lock();
    ble_serial_rx_cb_t cb = s->rx_cb;
    void *ctx = s->rx_ctx;
    if (cb == NULL) {
        /* Una escritura entra completa o no entra: nunca se trunca a la mitad. */
        if (len <= ring_free(&s->rx)) {
            ring_put(&s->rx, data, len);
        } else {
            s->rx_dropped += (uint32_t)len;
        }
    }
    unlock();

    if (cb == NULL) {
        xSemaphoreGive(s->rx_sem);
        return;
    }

    /* Modo callback: reensambla por '\n' fuera del mutex (solo esta tarea usa line[]). */
    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            deliver_line(cb, ctx);
        } else {
            if (s->line_len >= CONFIG_BLE_SERIAL_MAX_LINE) {
                deliver_line(cb, ctx); /* linea demasiado larga: se entrega en trozos */
            }
            s->line[s->line_len++] = c;
        }
    }
}

/* ---- API publica ------------------------------------------------------------------ */

esp_err_t ble_serial_init(const char *name)
{
    if (s != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (in_isr()) {
        return ESP_ERR_INVALID_STATE;
    }

    bsc_state_t *st = calloc(1, sizeof(*st));
    if (st == NULL) {
        return ESP_ERR_NO_MEM;
    }
    st->tx.buf = malloc(CONFIG_BLE_SERIAL_TX_BUFFER_SIZE);
    st->tx.cap = CONFIG_BLE_SERIAL_TX_BUFFER_SIZE;
    st->rx.buf = malloc(CONFIG_BLE_SERIAL_RX_BUFFER_SIZE);
    st->rx.cap = CONFIG_BLE_SERIAL_RX_BUFFER_SIZE;
    st->lock = xSemaphoreCreateMutex();
    st->rx_sem = xSemaphoreCreateBinary();
    st->tx_space = xSemaphoreCreateBinary();
    if (st->tx.buf == NULL || st->rx.buf == NULL || st->lock == NULL ||
        st->rx_sem == NULL || st->tx_space == NULL) {
        goto fail_mem;
    }
    st->mtu = BSC_MIN_MTU;
    st->tx_timeout_ms = CONFIG_BLE_SERIAL_TX_TIMEOUT_MS;
#ifdef CONFIG_BLE_SERIAL_BUFFER_WHILE_DISCONNECTED
    st->keep_offline = true;
#endif

    s = st;
    if (xTaskCreate(tx_task, "ble_serial_tx", CONFIG_BLE_SERIAL_TASK_STACK, NULL,
                    CONFIG_BLE_SERIAL_TASK_PRIORITY, &st->task) != pdPASS) {
        s = NULL;
        goto fail_mem;
    }

    char nm[BSC_MAX_NAME + 1];
    if (name == NULL || name[0] == '\0') {
        name = BLE_SERIAL_DEFAULT_NAME;
    }
    strncpy(nm, name, BSC_MAX_NAME);
    nm[BSC_MAX_NAME] = '\0';

    esp_err_t err = bsc_backend_start(nm);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no se pudo iniciar BLE: %s", esp_err_to_name(err));
        /* La tarea queda dormida; el estado se conserva para que write/read no fallen. */
    }
    return err;

fail_mem:
    if (st->lock) vSemaphoreDelete(st->lock);
    if (st->rx_sem) vSemaphoreDelete(st->rx_sem);
    if (st->tx_space) vSemaphoreDelete(st->tx_space);
    free(st->tx.buf);
    free(st->rx.buf);
    free(st);
    return ESP_ERR_NO_MEM;
}

size_t ble_serial_write(const void *data, size_t len)
{
    if (s == NULL || data == NULL || len == 0 || in_isr()) {
        return 0;
    }
    const uint8_t *p = (const uint8_t *)data;

    lock();
    if (!s->connected && !s->keep_offline) {
        unlock();
        return 0; /* sin conexion: se descarta, sin bloquear ni fallar */
    }

    /* Espera opcional (tx_timeout_ms) a que haya espacio, solo si alguien esta leyendo. */
    if (s->tx_timeout_ms > 0 && len <= s->tx.cap && ring_free(&s->tx) < len) {
        TickType_t start = xTaskGetTickCount();
        TickType_t limit = ms_to_ticks(s->tx_timeout_ms);
        while (ring_free(&s->tx) < len && s->connected && s->subscribed) {
            TickType_t elapsed = xTaskGetTickCount() - start;
            if (elapsed >= limit) {
                break;
            }
            unlock();
            xSemaphoreTake(s->tx_space, limit - elapsed);
            lock();
        }
    }

    size_t dropped = ring_put_overwrite(&s->tx, p, len);
    s->tx_dropped += (uint32_t)dropped;
    bool can_send = s->connected && s->subscribed;
    unlock();

    if (can_send) {
        wake_task();
    }
    return len;
}

int ble_serial_vprintf(const char *fmt, va_list ap)
{
    if (fmt == NULL) {
        return -1;
    }
    char small[128];
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(small, sizeof(small), fmt, ap);
    int ret;
    if (n < 0) {
        ret = -1;
    } else if ((size_t)n < sizeof(small)) {
        ret = (int)ble_serial_write(small, (size_t)n);
    } else {
        char *big = malloc((size_t)n + 1);
        if (big == NULL) {
            ret = -1;
        } else {
            vsnprintf(big, (size_t)n + 1, fmt, ap2);
            ret = (int)ble_serial_write(big, (size_t)n);
            free(big);
        }
    }
    va_end(ap2);
    return ret;
}

int ble_serial_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = ble_serial_vprintf(fmt, ap);
    va_end(ap);
    return n;
}

size_t ble_serial_available(void)
{
    if (s == NULL || in_isr()) {
        return 0;
    }
    lock();
    size_t n = s->rx.len;
    unlock();
    return n;
}

size_t ble_serial_read(void *buf, size_t maxlen, TickType_t timeout_ticks)
{
    if (s == NULL || buf == NULL || maxlen == 0 || in_isr()) {
        return 0;
    }
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        lock();
        size_t n = ring_get(&s->rx, (uint8_t *)buf, maxlen);
        unlock();
        if (n > 0) {
            return n;
        }
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (timeout_ticks == 0 || (timeout_ticks != portMAX_DELAY && elapsed >= timeout_ticks)) {
            return 0;
        }
        xSemaphoreTake(s->rx_sem, timeout_ticks == portMAX_DELAY ? portMAX_DELAY
                                                                 : timeout_ticks - elapsed);
    }
}

int ble_serial_read_line(char *buf, size_t maxlen, TickType_t timeout_ticks)
{
    if (s == NULL || buf == NULL || maxlen < 2 || in_isr()) {
        return -1;
    }
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        lock();
        size_t limit = maxlen - 1;
        size_t pos = ring_find(&s->rx, '\n', limit + 1);
        size_t take = 0;
        bool have = false;
        if (pos > 0) {
            take = pos; /* incluye el '\n' */
            have = true;
        } else if (s->rx.len >= limit || ring_free(&s->rx) == 0) {
            take = s->rx.len < limit ? s->rx.len : limit; /* sin '\n' y sin espacio */
            have = true;
        }
        if (have) {
            size_t n = ring_get(&s->rx, (uint8_t *)buf, take);
            unlock();
            if (n > 0 && buf[n - 1] == '\n') {
                n--;
            }
            if (n > 0 && buf[n - 1] == '\r') {
                n--;
            }
            buf[n] = '\0';
            return (int)n;
        }
        unlock();
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (timeout_ticks == 0 || (timeout_ticks != portMAX_DELAY && elapsed >= timeout_ticks)) {
            return -1;
        }
        xSemaphoreTake(s->rx_sem, timeout_ticks == portMAX_DELAY ? portMAX_DELAY
                                                                 : timeout_ticks - elapsed);
    }
}

int ble_serial_peek(void)
{
    if (s == NULL || in_isr()) {
        return -1;
    }
    lock();
    int c = s->rx.len > 0 ? (int)ring_at(&s->rx, 0) : -1;
    unlock();
    return c;
}

bool ble_serial_connected(void)
{
    if (s == NULL || in_isr()) {
        return false;
    }
    lock();
    bool c = s->connected;
    unlock();
    return c;
}

bool ble_serial_subscribed(void)
{
    if (s == NULL || in_isr()) {
        return false;
    }
    lock();
    bool c = s->connected && s->subscribed;
    unlock();
    return c;
}

uint16_t ble_serial_mtu(void)
{
    if (s == NULL || in_isr()) {
        return BSC_MIN_MTU;
    }
    lock();
    uint16_t m = s->mtu;
    unlock();
    return m;
}

uint32_t ble_serial_dropped(void)
{
    if (s == NULL || in_isr()) {
        return 0;
    }
    lock();
    uint32_t d = s->tx_dropped;
    unlock();
    return d;
}

uint32_t ble_serial_rx_dropped(void)
{
    if (s == NULL || in_isr()) {
        return 0;
    }
    lock();
    uint32_t d = s->rx_dropped;
    unlock();
    return d;
}

size_t ble_serial_tx_free(void)
{
    if (s == NULL || in_isr()) {
        return 0;
    }
    lock();
    size_t f = ring_free(&s->tx);
    unlock();
    return f;
}

void ble_serial_set_tx_timeout(uint32_t ms)
{
    if (s == NULL || in_isr()) {
        return;
    }
    lock();
    s->tx_timeout_ms = ms;
    unlock();
}

void ble_serial_set_buffer_when_disconnected(bool enable)
{
    if (s == NULL || in_isr()) {
        return;
    }
    lock();
    s->keep_offline = enable;
    unlock();
}

void ble_serial_set_rx_callback(ble_serial_rx_cb_t cb, void *ctx)
{
    if (s == NULL || in_isr()) {
        return;
    }
    lock();
    s->rx_cb = cb;
    s->rx_ctx = ctx;
    unlock();
}

bool ble_serial_flush(TickType_t timeout_ticks)
{
    if (s == NULL || in_isr()) {
        return false;
    }
    TickType_t start = xTaskGetTickCount();
    for (;;) {
        lock();
        bool empty = s->tx.len == 0 && s->pend_off >= s->pend_len;
        bool listening = s->connected && s->subscribed;
        unlock();
        if (empty) {
            return true;
        }
        if (!listening) {
            return false;
        }
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (timeout_ticks != portMAX_DELAY && elapsed >= timeout_ticks) {
            return false;
        }
        vTaskDelay(1);
    }
}

/* ---- Redireccion de ESP_LOGx ------------------------------------------------------ */

static int log_vprintf(const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int r = s_prev_vprintf != NULL ? s_prev_vprintf(fmt, ap) : vprintf(fmt, ap);

    /* No se reenvia desde una ISR, antes del planificador ni desde la propia tarea de
     * envio (el stack BLE registra cada notificacion y se haria un ciclo sin fin). */
    if (s != NULL && !in_isr() && xTaskGetSchedulerState() == taskSCHEDULER_RUNNING &&
        xTaskGetCurrentTaskHandle() != s->task) {
        char line[160];
        int n = vsnprintf(line, sizeof(line), fmt, ap2);
        if (n > 0) {
            size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
            ble_serial_write(line, len);
        }
    }
    va_end(ap2);
    return r;
}

void ble_serial_redirect_log(bool enable)
{
    if (in_isr()) {
        return;
    }
    if (enable && !s_log_redirected) {
        s_prev_vprintf = esp_log_set_vprintf(log_vprintf);
        s_log_redirected = true;
    } else if (!enable && s_log_redirected) {
        esp_log_set_vprintf(s_prev_vprintf != NULL ? s_prev_vprintf : vprintf);
        s_prev_vprintf = NULL;
        s_log_redirected = false;
    }
}
