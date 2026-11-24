/*
 * ble_serial_bluedroid.cpp - backend Arduino de BLESerial con la biblioteca BLE del
 * core arduino-esp32 (BLEDevice, stack Bluedroid). Sin dependencias externas.
 * Funciona con el core 2.x y con el 3.x; las diferencias van con
 * ESP_ARDUINO_VERSION_MAJOR.
 * En ESP-IDF, o en un core Arduino compilado con NimBLE en lugar de Bluedroid, este
 * archivo queda vacio y se usa ble_serial_nimble.c.
 *
 * Licencia MIT.
 */
#include "sdkconfig.h"

#if defined(ARDUINO) && defined(CONFIG_BLUEDROID_ENABLED)

#include "esp_arduino_version.h"

/* Las cabeceras BLE del core tienen parametros sin usar; se silencian solo aqui
 * para que -Wextra no las reporte como si fueran de esta biblioteca. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#pragma GCC diagnostic pop

#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"

#include "ble_serial.h"
#include "ble_serial_core.h"

namespace {

const uint16_t CONN_NONE = 0xFFFF;

BLEServer *g_server = nullptr;
BLECharacteristic *g_tx = nullptr;
BLE2902 *g_cccd = nullptr;
volatile uint16_t g_conn = CONN_NONE;
volatile bool g_congested = false;
volatile esp_gatt_if_t g_gatts_if = ESP_GATT_IF_NONE; /* BLEServer::getGattsIf() es privado */

class ServerCallbacks : public BLEServerCallbacks {
public:
    using BLEServerCallbacks::onConnect;
    using BLEServerCallbacks::onDisconnect;

    void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
        (void)server;
        g_conn = param->connect.conn_id;
        g_congested = false;
        bsc_on_connect();
    }

    void onDisconnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
        (void)param;
        g_conn = CONN_NONE;
        g_congested = false;
        if (g_cccd != nullptr) {
            g_cccd->setNotifications(false); /* sin bonding: la suscripcion no persiste */
        }
        bsc_on_disconnect();
        server->startAdvertising(); /* contrato: se reanuda el advertising solo */
    }

    void onMtuChanged(BLEServer *server, esp_ble_gatts_cb_param_t *param) override
    {
        (void)server;
        bsc_on_mtu(param->mtu.mtu);
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
public:
    using BLECharacteristicCallbacks::onWrite;

    void onWrite(BLECharacteristic *chr) override
    {
        /* getData()/getLength() existen igual en el core 2.x y en el 3.x
         * (getValue() cambio de std::string a String). */
        bsc_on_rx(chr->getData(), chr->getLength());
    }
};

class CccdCallbacks : public BLEDescriptorCallbacks {
public:
    void onWrite(BLEDescriptor *desc) override
    {
        bsc_on_subscribe(static_cast<BLE2902 *>(desc)->getNotifications());
    }
};

ServerCallbacks g_server_cb;
RxCallbacks g_rx_cb;
CccdCallbacks g_cccd_cb;

/* Manejador GATTS adicional: guarda el gatts_if y avisa cuando Bluedroid se congestiona. */
void gatts_extra(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                 esp_ble_gatts_cb_param_t *param)
{
    if (event == ESP_GATTS_REG_EVT || event == ESP_GATTS_CONNECT_EVT) {
        g_gatts_if = gatts_if;
    } else if (event == ESP_GATTS_CONGEST_EVT) {
        g_congested = param->congest.congested;
        if (!g_congested) {
            bsc_on_tx_ready();
        }
    }
}

} // namespace

extern "C" esp_err_t bsc_backend_start(const char *name)
{
    BLEDevice::init(name);
    /* Se acepta hasta CONFIG_BLE_SERIAL_LOCAL_MTU (517); el cliente pide el suyo. */
    BLEDevice::setMTU(CONFIG_BLE_SERIAL_LOCAL_MTU);
    BLEDevice::setCustomGattsHandler(gatts_extra);

    g_server = BLEDevice::createServer();
    if (g_server == nullptr) {
        return ESP_FAIL;
    }
    g_server->setCallbacks(&g_server_cb);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    /* El core 3.x puede reanudar el advertising por su cuenta; aqui lo hace
     * onDisconnect() para que el comportamiento sea igual en los dos cores. */
    g_server->advertiseOnDisconnect(false);
#endif

    BLEService *svc = g_server->createService(BLE_SERIAL_SERVICE_UUID);
    BLECharacteristic *rx = svc->createCharacteristic(
        BLE_SERIAL_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(&g_rx_cb);

    g_tx = svc->createCharacteristic(BLE_SERIAL_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    g_cccd = new BLE2902();
    g_cccd->setCallbacks(&g_cccd_cb);
    g_tx->addDescriptor(g_cccd);
    svc->start();

    /* Paquete de advertising: flags + UUID de 128 bits. Scan response: nombre.
     * (El modo por defecto de BLEAdvertising repite el UUID en el scan response y
     * no cabe un nombre de 12 caracteres; por eso se arman a mano.) */
    BLEAdvertising *adv = BLEDevice::getAdvertising();
    BLEAdvertisementData ad;
    ad.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    ad.setCompleteServices(BLEUUID(BLE_SERIAL_SERVICE_UUID));
    adv->setAdvertisementData(ad);
    BLEAdvertisementData sr;
    sr.setName(name);
    adv->setScanResponseData(sr);
    adv->start();
    return ESP_OK;
}

extern "C" bsc_send_result_t bsc_backend_send(const uint8_t *data, size_t len)
{
    uint16_t conn = g_conn;
    esp_gatt_if_t gif = g_gatts_if;
    if (conn == CONN_NONE || gif == ESP_GATT_IF_NONE || g_tx == nullptr) {
        return BSC_SEND_FAIL;
    }
    if (g_congested || esp_ble_get_cur_sendable_packets_num(conn) == 0) {
        return BSC_SEND_BUSY;
    }
    /* need_confirm = false -> notificacion. Bluedroid copia el valor. */
    esp_err_t err = esp_ble_gatts_send_indicate(gif, conn, g_tx->getHandle(),
                                                static_cast<uint16_t>(len),
                                                const_cast<uint8_t *>(data), false);
    return err == ESP_OK ? BSC_SEND_OK : BSC_SEND_BUSY;
}

#endif /* ARDUINO && CONFIG_BLUEDROID_ENABLED */
