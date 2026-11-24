/*
 * ble_serial_nimble.c - backend de BLESerial con el stack NimBLE (API del host).
 * Se usa en ESP-IDF y en un core Arduino compilado con NimBLE en lugar de Bluedroid.
 * En Arduino con Bluedroid (lo normal) este archivo queda vacio y se usa
 * ble_serial_bluedroid.cpp.
 *
 * Requiere en sdkconfig: CONFIG_BT_ENABLED=y, CONFIG_BT_NIMBLE_ENABLED=y.
 *
 * Licencia MIT.
 */
#include "sdkconfig.h"

#if !defined(ARDUINO) || !defined(CONFIG_BLUEDROID_ENABLED)

#if !defined(CONFIG_BT_NIMBLE_ENABLED)
#error "BLESerial requiere CONFIG_BT_ENABLED=y y CONFIG_BT_NIMBLE_ENABLED=y en ESP-IDF, o Bluedroid en Arduino (ver README)"
#endif

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "ble_serial_core.h"

static const char *TAG = "ble_serial";

/* UUIDs NUS en orden de bytes little-endian (como los espera NimBLE). */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);
static const ble_uuid128_t s_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t s_tx_handle;
static volatile uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static char s_name[BSC_MAX_NAME + 1];

static int gap_event(struct ble_gap_event *event, void *arg);

/* ---- GATT: servicio NUS ----------------------------------------------------------- */

static int chr_access(uint16_t conn_handle, uint16_t attr_handle,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        /* El valor puede venir en una cadena de mbufs: se pasa cada tramo tal cual. */
        for (const struct os_mbuf *m = ctxt->om; m != NULL; m = SLIST_NEXT(m, om_next)) {
            bsc_on_rx(m->om_data, m->om_len);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_chr_def s_chrs[] = {
    {
        .uuid = &s_rx_uuid.u,
        .access_cb = chr_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        /* NimBLE agrega el CCCD (0x2902) solo por tener NOTIFY. */
        .uuid = &s_tx_uuid.u,
        .access_cb = chr_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = s_chrs,
    },
    {0},
};

/* ---- Advertising: UUID en el paquete, nombre en el scan response ------------------ */

static void advertise(void)
{
    struct ble_hs_adv_fields adv;
    memset(&adv, 0, sizeof(adv));
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &s_svc_uuid;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields rc=%d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = (uint8_t)strlen(s_name);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start rc=%d", rc);
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            bsc_on_connect();
            bsc_on_mtu(ble_att_mtu(s_conn));
        } else {
            advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        bsc_on_disconnect();
        advertise(); /* contrato: se reanuda el advertising solo */
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
            advertise();
        }
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_handle) {
            bsc_on_subscribe(event->subscribe.cur_notify != 0);
        }
        break;

    case BLE_GAP_EVENT_MTU:
        bsc_on_mtu(event->mtu.value);
        break;

    case BLE_GAP_EVENT_NOTIFY_TX:
        bsc_on_tx_ready();
        break;

    default:
        break;
    }
    return 0;
}

/* ---- Arranque del host ------------------------------------------------------------ */

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "sin direccion BLE rc=%d", rc);
        return;
    }
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "reinicio del host NimBLE, motivo=%d", reason);
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run(); /* regresa solo con nimble_port_stop() */
    nimble_port_freertos_deinit();
}

esp_err_t bsc_backend_start(const char *name)
{
    strncpy(s_name, name, BSC_MAX_NAME);
    s_name[BSC_MAX_NAME] = '\0';

    /* El controlador usa NVS para la calibracion de RF. Si la aplicacion ya la inicio,
     * esto no hace nada; si la particion tiene problemas NO se borra: solo se avisa. */
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_flash_init: %s (se continua)", esp_err_to_name(err));
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    /* v1: sin emparejamiento ni cifrado. */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_svcs);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "no se pudo registrar el servicio NUS rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_svc_gap_device_name_set(s_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "device_name_set rc=%d", rc);
    }

    /* Se acepta hasta CONFIG_BLE_SERIAL_LOCAL_MTU (517); el cliente pide el suyo. */
    rc = ble_att_set_preferred_mtu(CONFIG_BLE_SERIAL_LOCAL_MTU);
    if (rc != 0) {
        ESP_LOGW(TAG, "set_preferred_mtu rc=%d", rc);
    }

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

bsc_send_result_t bsc_backend_send(const uint8_t *data, size_t len)
{
    uint16_t conn = s_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) {
        return BSC_SEND_FAIL;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
    if (om == NULL) {
        return BSC_SEND_BUSY; /* sin mbufs libres: el stack aun esta enviando */
    }
    /* ble_gatts_notify_custom consume om pase lo que pase. */
    int rc = ble_gatts_notify_custom(conn, s_tx_handle, om);
    if (rc == 0) {
        return BSC_SEND_OK;
    }
    if (rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY) {
        return BSC_SEND_BUSY;
    }
    return BSC_SEND_FAIL;
}

#endif /* !ARDUINO || !CONFIG_BLUEDROID_ENABLED */
