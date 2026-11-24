/*
 * BLESerial.cpp - objeto Stream de Arduino sobre la API en C de ble_serial.h.
 * En ESP-IDF la envoltura C++ es solo de cabecera y este archivo queda vacio.
 *
 * Licencia MIT.
 */
#if defined(ARDUINO)

#include "BLESerial.h"

BLESerialClass BLESerial;

bool BLESerialClass::begin(const char *name)
{
    return ble_serial_init(name) == ESP_OK;
}

int BLESerialClass::available()
{
    return (int)ble_serial_available();
}

int BLESerialClass::read()
{
    uint8_t b;
    return ble_serial_read(&b, 1, 0) == 1 ? b : -1;
}

int BLESerialClass::peek()
{
    return ble_serial_peek();
}

void BLESerialClass::flush()
{
    ble_serial_flush(pdMS_TO_TICKS(1000));
}

size_t BLESerialClass::write(uint8_t b)
{
    return ble_serial_write(&b, 1);
}

size_t BLESerialClass::write(const uint8_t *buffer, size_t size)
{
    return ble_serial_write(buffer, size);
}

int BLESerialClass::availableForWrite()
{
    return (int)ble_serial_tx_free();
}

bool BLESerialClass::connected()
{
    return ble_serial_connected();
}

bool BLESerialClass::subscribed()
{
    return ble_serial_subscribed();
}

uint16_t BLESerialClass::mtu()
{
    return ble_serial_mtu();
}

uint32_t BLESerialClass::droppedBytes()
{
    return ble_serial_dropped();
}

void BLESerialClass::setTxTimeout(uint32_t ms)
{
    ble_serial_set_tx_timeout(ms);
}

void BLESerialClass::setBufferWhenDisconnected(bool enable)
{
    ble_serial_set_buffer_when_disconnected(enable);
}

void BLESerialClass::setRxCallback(ble_serial_rx_cb_t cb, void *ctx)
{
    ble_serial_set_rx_callback(cb, ctx);
}

void BLESerialClass::redirectLog(bool enable)
{
    ble_serial_redirect_log(enable);
}

#endif /* ARDUINO */
