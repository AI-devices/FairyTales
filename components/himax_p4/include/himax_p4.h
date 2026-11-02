#pragma once

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "i2c_protocol.h"

class AudioPlayerP4;

class HimaxP4
{
public:
    HimaxP4();

    bool init(AudioPlayerP4 *player);
    bool ready() const;

    esp_err_t probe(int timeout_ms = 100);
    size_t scanBus(int timeout_ms = 20);
    esp_err_t getStatus(uint8_t &status);
    esp_err_t sendText(const char *text, TickType_t wait_ticks);
    esp_err_t start();
    esp_err_t stop();
    esp_err_t pause();
    esp_err_t resume();
    esp_err_t waitUntilIdle(TickType_t timeout_ticks);
    esp_err_t readPacket(data_packet_t &packet);

private:
    static void taskEntry(void *arg);

    esp_err_t initTransport();
    esp_err_t resetDevice();
    esp_err_t waitReady(TickType_t timeout_ticks);
    esp_err_t sendCommand(uint8_t cmd, uint16_t data_length, int timeout_ms);
    esp_err_t recoverBus(const char *reason, esp_err_t err);
    esp_err_t recoverAfterStreamError(void);
    esp_err_t probeAndReadStatusWithRetry(uint8_t &status);
    esp_err_t probeAddress(uint8_t addr, int timeout_ms);
    esp_err_t getStatusWithRetry(uint8_t &status, int retries);
    esp_err_t readPacketWithRetry(data_packet_t &packet, int retries);
    bool lock(TickType_t timeout_ticks);
    void unlock();

private:
    static constexpr char TAG[] = "HimaxP4";
    static constexpr EventBits_t STATUS_BUSY = BIT0;
    static constexpr EventBits_t STATUS_PAUSED = BIT1;

    AudioPlayerP4 *player_;
    EventGroupHandle_t status_;
    SemaphoreHandle_t lock_;
    TaskHandle_t task_;
    i2c_master_bus_handle_t bus_;
    i2c_master_dev_handle_t dev_;
    gpio_num_t reset_gpio_;
    bool have_reset_;
    bool ready_;
    bool bus_reset_failed_;
    uint32_t reset_counter_;
};
