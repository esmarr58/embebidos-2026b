/*
 * BLESerial.h - puerto serie sobre BLE (Nordic UART Service) para ESP32-S3.
 *
 * Arduino: objeto global BLESerial que hereda de Stream (print, println, printf, write,
 *          available, read, peek, readStringUntil, flush... vienen dados).
 * ESP-IDF: envoltura C++ opcional con los mismos nombres, sobre la API en C de
 *          ble_serial.h (requiere C++17, que es el defecto de ESP-IDF 5.x).
 *
 * Licencia MIT.
 */
#pragma once

#include "ble_serial.h"

#if defined(ARDUINO)

#include <Arduino.h>

class BLESerialClass : public Stream {
public:
    /* Inicia BLE y el advertising. Devuelve false si fallo o ya estaba iniciado. */
    bool begin(const char *name = BLE_SERIAL_DEFAULT_NAME);

    /* Stream / Print */
    int available() override;
    int read() override;
    int peek() override;
    void flush() override; /* espera a que salga el bufer TX (max. 1 s, solo si hay cliente) */
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    int availableForWrite() override;
    using Print::write;

    /* Estado */
    bool connected();
    bool subscribed();
    explicit operator bool() { return connected(); }
    uint16_t mtu();
    uint32_t droppedBytes();

    /* Ajustes */
    void setTxTimeout(uint32_t ms);
    void setBufferWhenDisconnected(bool enable);
    void setRxCallback(ble_serial_rx_cb_t cb, void *ctx = nullptr);
    void redirectLog(bool enable);
};

extern BLESerialClass BLESerial;

#else /* ESP-IDF */

#ifdef __cplusplus
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

class BLESerialClass {
public:
    bool begin(const char *name = BLE_SERIAL_DEFAULT_NAME) { return ble_serial_init(name) == ESP_OK; }

    size_t write(uint8_t b) { return ble_serial_write(&b, 1); }
    size_t write(const uint8_t *buf, size_t len) { return ble_serial_write(buf, len); }
    size_t print(const char *s) { return ble_serial_write(s, std::strlen(s)); }
    size_t print(const std::string &s) { return ble_serial_write(s.data(), s.size()); }
    size_t print(char c) { return ble_serial_write(&c, 1); }
    size_t print(long v) { return (size_t)printf("%ld", v); }
    size_t print(int v) { return print((long)v); }
    size_t print(unsigned long v) { return (size_t)printf("%lu", v); }
    size_t print(unsigned int v) { return print((unsigned long)v); }
    size_t print(double v, int digits = 2) { return (size_t)printf("%.*f", digits, v); }
    template <typename T> size_t println(const T &v) { size_t n = print(v); return n + print('\n'); }
    size_t println(double v, int digits) { size_t n = print(v, digits); return n + print('\n'); }
    size_t println() { return print('\n'); }

    int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)))
    {
        va_list ap;
        va_start(ap, fmt);
        int n = ble_serial_vprintf(fmt, ap);
        va_end(ap);
        return n;
    }

    int available() { return (int)ble_serial_available(); }
    int read()
    {
        uint8_t b;
        return ble_serial_read(&b, 1, 0) == 1 ? b : -1;
    }
    int peek() { return ble_serial_peek(); }
    void flush() { ble_serial_flush(pdMS_TO_TICKS(1000)); }

    /* Lee hasta 'term' (sin incluirlo) o hasta que pasen timeout ms sin datos. */
    std::string readStringUntil(char term)
    {
        std::string out;
        for (;;) {
            uint8_t b;
            if (ble_serial_read(&b, 1, pdMS_TO_TICKS(timeout_ms_)) != 1 || (char)b == term) {
                return out;
            }
            out.push_back((char)b);
        }
    }
    void setTimeout(uint32_t ms) { timeout_ms_ = ms; }

    bool connected() { return ble_serial_connected(); }
    bool subscribed() { return ble_serial_subscribed(); }
    explicit operator bool() { return connected(); }
    uint16_t mtu() { return ble_serial_mtu(); }
    uint32_t droppedBytes() { return ble_serial_dropped(); }
    void setTxTimeout(uint32_t ms) { ble_serial_set_tx_timeout(ms); }
    void setBufferWhenDisconnected(bool enable) { ble_serial_set_buffer_when_disconnected(enable); }
    void setRxCallback(ble_serial_rx_cb_t cb, void *ctx = nullptr) { ble_serial_set_rx_callback(cb, ctx); }
    void redirectLog(bool enable) { ble_serial_redirect_log(enable); }

private:
    uint32_t timeout_ms_ = 1000;
};

inline BLESerialClass BLESerial;

#endif /* __cplusplus */
#endif /* ARDUINO */
