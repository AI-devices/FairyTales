#include <string.h>

#include "sdkconfig.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

#include "audio_player_p4.h"
#include "bsp/esp-bsp.h"
#include "crc.h"
#include "crc_table.h"
#include "himax_p4.h"

extern "C" void ui_story_bird_talk_anim_stop(void);
extern "C" void ui_notify_tts_finished(void);
extern "C" void audio_session_notify_error(esp_err_t error);

namespace {
constexpr uint32_t kHimaxI2cSpeedHz = 400000;
constexpr uint32_t kHimaxSclWaitUs = 100000;
constexpr int kOpTimeoutMs = 300;
constexpr TickType_t kStatusPollDelay = pdMS_TO_TICKS(14);
constexpr TickType_t kReadyPollDelay = pdMS_TO_TICKS(120);
constexpr TickType_t kResetHoldDelay = pdMS_TO_TICKS(100);
constexpr TickType_t kResetSettleDelay = pdMS_TO_TICKS(1200);
constexpr TickType_t kProbeRetryDelay = pdMS_TO_TICKS(250);
constexpr TickType_t kProbeExtraBusReleaseDelay = pdMS_TO_TICKS(500);
constexpr int kProbeRetryCount = 4;
constexpr TickType_t kStreamRetryDelay = pdMS_TO_TICKS(40);
constexpr int kStreamRetryCount = 3;
constexpr uint64_t kAudioWriteBlockWarnUs = 30000;
}

constexpr char HimaxP4::TAG[];

HimaxP4::HimaxP4() :
    player_(nullptr),
    status_(nullptr),
    lock_(nullptr),
    task_(nullptr),
    bus_(nullptr),
    dev_(nullptr),
    reset_gpio_(GPIO_NUM_NC),
    have_reset_(false),
    ready_(false),
    bus_reset_failed_(false),
    reset_counter_(0)
{
#if CONFIG_HIMAX_HAVE_RESET
    have_reset_ = true;
    reset_gpio_ = static_cast<gpio_num_t>(CONFIG_HIMAX_RESET_GPIO);
#endif
}

bool HimaxP4::init(AudioPlayerP4 *player)
{
    player_ = player;

    if (!status_) {
        status_ = xEventGroupCreate();
        if (!status_) {
            ESP_LOGE(TAG, "Failed to create status event group");
            return false;
        }
    }

    if (!lock_) {
        lock_ = xSemaphoreCreateMutex();
        if (!lock_) {
            ESP_LOGE(TAG, "Failed to create Himax mutex");
            return false;
        }
    }

    if (initTransport() != ESP_OK) {
        return false;
    }

    if (have_reset_) {
        gpio_reset_pin(reset_gpio_);
        gpio_set_direction(reset_gpio_, GPIO_MODE_INPUT_OUTPUT_OD);
        gpio_set_pull_mode(reset_gpio_, GPIO_PULLUP_ONLY);
        gpio_set_level(reset_gpio_, 1);
        vTaskDelay(pdMS_TO_TICKS(20));
        if (resetDevice() != ESP_OK) {
            return false;
        }
    }

    uint8_t status = 0;
    if (probeAndReadStatusWithRetry(status) != ESP_OK) {
        ESP_LOGE(TAG, "Himax init failed after %d probe attempt(s)", kProbeRetryCount);
        return false;
    }

    ready_ = true;

    if (!task_) {
        BaseType_t rc = xTaskCreate(taskEntry, "himax_p4_rx", 6144, this, 5, &task_);
        if (rc != pdPASS) {
            ESP_LOGE(TAG, "Failed to create Himax task");
            task_ = nullptr;
            ready_ = false;
            return false;
        }
    }

    ESP_LOGI(TAG, "Himax init complete, status=%s", status_to_str(status));
    return true;
}

bool HimaxP4::ready() const
{
    return ready_;
}

esp_err_t HimaxP4::initTransport()
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "Failed to init BSP I2C");

    bus_ = bsp_i2c_get_handle();
    if (!bus_) {
        ESP_LOGE(TAG, "BSP I2C handle is null");
        return ESP_ERR_INVALID_STATE;
    }

    if (!dev_) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = CONFIG_HIMAX_I2C_ADDR,
            .scl_speed_hz = kHimaxI2cSpeedHz,
            .scl_wait_us = kHimaxSclWaitUs,
            .flags = {
                .disable_ack_check = 0,
            },
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus_, &dev_cfg, &dev_), TAG, "Add Himax device failed");
    }

    return ESP_OK;
}

esp_err_t HimaxP4::resetDevice()
{
    if (!have_reset_) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Reset Himax module #%lu on GPIO %d", (unsigned long)reset_counter_++, reset_gpio_);
    gpio_set_level(reset_gpio_, 0);
    vTaskDelay(kResetHoldDelay);
    gpio_set_level(reset_gpio_, 1);
    for (int i = 0; i < 10 && gpio_get_level(reset_gpio_) == 0; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(kResetSettleDelay);
    if (gpio_get_level(reset_gpio_) == 0) {
        ESP_LOGW(TAG, "Reset line stayed low after release; pin may be externally held or not be Himax RES");
    }

    return ESP_OK;
}

bool HimaxP4::lock(TickType_t timeout_ticks)
{
    return lock_ && xSemaphoreTake(lock_, timeout_ticks) == pdTRUE;
}

void HimaxP4::unlock()
{
    if (lock_) {
        xSemaphoreGive(lock_);
    }
}

esp_err_t HimaxP4::recoverBus(const char *reason, esp_err_t err)
{
    if ((err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE) && bus_) {
        esp_err_t reset_err = ESP_OK;

        ESP_LOGW(TAG,
                 "%s failed with %s, resetting I2C bus",
                 reason ? reason : "<null>",
                 esp_err_to_name(err));
        reset_err = i2c_master_bus_reset(bus_);
        bus_reset_failed_ = reset_err != ESP_OK;
        if (reset_err != ESP_OK) {
            ESP_LOGE(TAG,
                     "%s bus reset failed with %s",
                     reason ? reason : "<null>",
                     esp_err_to_name(reset_err));
        }
    }
    return err;
}

esp_err_t HimaxP4::recoverAfterStreamError()
{
    uint8_t status = 0xff;
    esp_err_t err = ESP_FAIL;

    ready_ = false;
    bus_reset_failed_ = false;
    xEventGroupClearBits(status_, STATUS_BUSY | STATUS_PAUSED);

    if (bus_) {
        esp_err_t reset_err = i2c_master_bus_reset(bus_);
        if (reset_err != ESP_OK) {
            ESP_LOGW(TAG, "Recovery bus reset failed with %s", esp_err_to_name(reset_err));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (have_reset_) {
        err = resetDevice();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Himax reset failed during recovery: %s", esp_err_to_name(err));
            return err;
        }
    }

    err = probeAndReadStatusWithRetry(status);
    if (err == ESP_OK) {
        ready_ = true;
        ESP_LOGW(TAG, "Himax recovered after stream error, status=%s", status_to_str(status));
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Himax recovery failed after stream error: %s", esp_err_to_name(err));
    return err;
}

esp_err_t HimaxP4::probe(int timeout_ms)
{
    return probeAddress(CONFIG_HIMAX_I2C_ADDR, timeout_ms);
}

size_t HimaxP4::scanBus(int timeout_ms)
{
    if (!bus_) {
        ESP_LOGW(TAG, "I2C bus scan skipped: bus is not initialized");
        return 0;
    }

    size_t found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        esp_err_t err = probeAddress(addr, timeout_ms);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "I2C device detected at 0x%02X", addr);
            ++found;
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "I2C bus scan found no responsive devices");
    } else {
        ESP_LOGI(TAG, "I2C bus scan complete, found %u device(s)", static_cast<unsigned>(found));
    }

    return found;
}

esp_err_t HimaxP4::probeAddress(uint8_t addr, int timeout_ms)
{
    if (!bus_) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = i2c_master_probe(bus_, addr, timeout_ms);
    if (err != ESP_OK) {
        return recoverBus("Probe", err);
    }
    return ESP_OK;
}

esp_err_t HimaxP4::probeAndReadStatusWithRetry(uint8_t &status)
{
    esp_err_t last_err = ESP_FAIL;

    for (int attempt = 1; attempt <= kProbeRetryCount; ++attempt) {
        const bool bus_busy = gpio_get_level(BSP_I2C_SDA) == 0 || gpio_get_level(BSP_I2C_SCL) == 0;
        if (bus_busy) {
            vTaskDelay(kProbeExtraBusReleaseDelay);
        }

        last_err = probe();
        if (last_err == ESP_OK) {
            last_err = getStatus(status);
            if (last_err == ESP_OK) {
                if (attempt > 1) {
                    ESP_LOGW(TAG, "Himax responded on retry %d/%d", attempt, kProbeRetryCount);
                }
                return ESP_OK;
            }
        }

        if (attempt < kProbeRetryCount) {
            ESP_LOGW(TAG, "Probe/status attempt %d/%d failed: %s", attempt, kProbeRetryCount, esp_err_to_name(last_err));
            vTaskDelay(kProbeRetryDelay);
        }
    }

    return last_err;
}

esp_err_t HimaxP4::sendCommand(uint8_t cmd, uint16_t data_length, int timeout_ms)
{
    if (!dev_) {
        return ESP_ERR_INVALID_STATE;
    }

    transaction_t req = {
        .cmd = cmd,
        .data_length = data_length,
    };

    if (!lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev_, reinterpret_cast<const uint8_t *>(&req), sizeof(req), timeout_ms);
    unlock();

    if (err != ESP_OK) {
        return recoverBus(cmd_to_str(cmd), err);
    }
    return ESP_OK;
}

esp_err_t HimaxP4::getStatus(uint8_t &status)
{
    if (!dev_) {
        return ESP_ERR_INVALID_STATE;
    }

    transaction_t req = {
        .cmd = CMD_GET_STATUS,
        .data_length = 0,
    };
    transaction_t resp = {};

    if (!lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev_, reinterpret_cast<const uint8_t *>(&req), sizeof(req), kOpTimeoutMs);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev_, reinterpret_cast<uint8_t *>(&resp), sizeof(resp), kOpTimeoutMs);
    }
    unlock();

    if (err != ESP_OK) {
        return recoverBus("GET_STATUS", err);
    }

    status = resp.status;
    return ESP_OK;
}

esp_err_t HimaxP4::sendText(const char *text, TickType_t wait_ticks)
{
    if (!ready_) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }

    const EventBits_t bits = xEventGroupGetBits(status_);
    if (bits & STATUS_BUSY) {
        return ESP_ERR_INVALID_STATE;
    }

    if (bits & STATUS_PAUSED) {
        ESP_RETURN_ON_ERROR(stop(), TAG, "Stop before new text failed");
        ESP_RETURN_ON_ERROR(waitReady(wait_ticks), TAG, "Wait ready failed");
    }

    const size_t len = strlen(text);
    transaction_t req = {
        .cmd = CMD_RECV_MSG,
        .data_length = static_cast<uint16_t>(len),
    };

    if (!lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev_, reinterpret_cast<const uint8_t *>(&req), sizeof(req), kOpTimeoutMs);
    if (err == ESP_OK) {
        err = i2c_master_transmit(dev_, reinterpret_cast<const uint8_t *>(text), len, static_cast<int>(pdTICKS_TO_MS(wait_ticks)));
    }
    unlock();

    if (err != ESP_OK) {
        return recoverBus("SEND_TEXT", err);
    }

    return ESP_OK;
}

esp_err_t HimaxP4::start()
{
    bus_reset_failed_ = false;
    ESP_RETURN_ON_ERROR(sendCommand(CMD_START, 0, kOpTimeoutMs), TAG, "Start command failed");
    xEventGroupSetBits(status_, STATUS_BUSY);
    xEventGroupClearBits(status_, STATUS_PAUSED);
    return ESP_OK;
}

esp_err_t HimaxP4::stop()
{
    ESP_RETURN_ON_ERROR(sendCommand(CMD_STOP, 0, kOpTimeoutMs), TAG, "Stop command failed");
    xEventGroupClearBits(status_, STATUS_BUSY | STATUS_PAUSED);
    if (player_) {
        player_->stop();
    }
    return ESP_OK;
}

esp_err_t HimaxP4::pause()
{
    const EventBits_t bits = xEventGroupGetBits(status_);
    if ((bits & STATUS_PAUSED) || !(bits & STATUS_BUSY)) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(sendCommand(CMD_PAUSE, 0, kOpTimeoutMs), TAG, "Pause command failed");
    xEventGroupSetBits(status_, STATUS_PAUSED);
    if (player_) {
        player_->pause();
    }
    return ESP_OK;
}

esp_err_t HimaxP4::resume()
{
    const EventBits_t bits = xEventGroupGetBits(status_);
    if (!(bits & STATUS_PAUSED)) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(sendCommand(CMD_RESUME, 0, kOpTimeoutMs), TAG, "Resume command failed");
    xEventGroupClearBits(status_, STATUS_PAUSED);
    xEventGroupSetBits(status_, STATUS_BUSY);
    if (player_) {
        player_->resume();
    }
    return ESP_OK;
}

esp_err_t HimaxP4::waitUntilIdle(TickType_t timeout_ticks)
{
    return waitReady(timeout_ticks);
}

esp_err_t HimaxP4::readPacket(data_packet_t &packet)
{
    if (!dev_) {
        return ESP_ERR_INVALID_STATE;
    }

    transaction_t req = {
        .cmd = CMD_SEND_DATA,
        .data_length = 0,
    };
    memset(&packet, 0, sizeof(packet));

    if (!lock(portMAX_DELAY)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(dev_, reinterpret_cast<const uint8_t *>(&req), sizeof(req), kOpTimeoutMs);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev_, reinterpret_cast<uint8_t *>(&packet), sizeof(packet), kOpTimeoutMs);
    }
    unlock();

    if (err != ESP_OK) {
        return recoverBus("SEND_DATA", err);
    }
    return ESP_OK;
}

esp_err_t HimaxP4::waitReady(TickType_t timeout_ticks)
{
    TickType_t elapsed = 0;
    uint8_t status = 0xff;

    while (elapsed <= timeout_ticks) {
        esp_err_t err = getStatus(status);
        if (err != ESP_OK) {
            return err;
        }
        if (status == ST_IDLE) {
            return ESP_OK;
        }

        vTaskDelay(kReadyPollDelay);
        elapsed += kReadyPollDelay;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t HimaxP4::getStatusWithRetry(uint8_t &status, int retries)
{
    esp_err_t last_err = ESP_FAIL;

    for (int attempt = 1; attempt <= retries; ++attempt) {
        last_err = getStatus(status);
        if (last_err == ESP_OK) {
            return ESP_OK;
        }

        if (attempt < retries) {
            if (bus_reset_failed_) {
                ESP_LOGW(TAG, "GET_STATUS aborting retries after failed bus reset");
                break;
            }
            ESP_LOGW(TAG, "GET_STATUS retry %d/%d after %s", attempt, retries, esp_err_to_name(last_err));
            vTaskDelay(kStreamRetryDelay);
        }
    }

    return last_err;
}

esp_err_t HimaxP4::readPacketWithRetry(data_packet_t &packet, int retries)
{
    esp_err_t last_err = ESP_FAIL;

    for (int attempt = 1; attempt <= retries; ++attempt) {
        last_err = readPacket(packet);
        if (last_err == ESP_OK) {
            return ESP_OK;
        }

        if (attempt < retries) {
            if (bus_reset_failed_) {
                ESP_LOGW(TAG, "SEND_DATA aborting retries after failed bus reset");
                break;
            }
            ESP_LOGW(TAG, "SEND_DATA retry %d/%d after %s", attempt, retries, esp_err_to_name(last_err));
            vTaskDelay(kStreamRetryDelay);
        }
    }

    return last_err;
}

void HimaxP4::taskEntry(void *arg)
{
    auto *self = static_cast<HimaxP4 *>(arg);

    for (;;) {
        xEventGroupWaitBits(self->status_, STATUS_BUSY, pdFALSE, pdFALSE, portMAX_DELAY);

        size_t packet_counter = 0;
        size_t corrupted_packets = 0;
        bool stop = false;
        bool paused = false;
        bool abort_due_to_i2c = false;
        TickType_t last_wake = xTaskGetTickCount();

        self->bus_reset_failed_ = false;
        ESP_LOGI(TAG, "Starting Himax receive loop");

        while (!stop) {
            if (xEventGroupGetBits(self->status_) & STATUS_PAUSED) {
                paused = true;
                break;
            }

            uint8_t status = 0xff;
            esp_err_t err = self->getStatusWithRetry(status, kStreamRetryCount);
            if (err != ESP_OK) {
                abort_due_to_i2c = true;
                break;
            }

            switch (status) {
            case ST_OUTPUT_RDY: {
                while (true) {
                    data_packet_t packet = {};
                    err = self->readPacketWithRetry(packet, kStreamRetryCount);
                    if (err != ESP_OK) {
                        abort_due_to_i2c = true;
                        break;
                    }
                    if (packet.error != ERR_OK) {
                        break;
                    }

                    uint16_t crc = crc16_compute(crc16_lut, &crc16_ccitt_false_config, packet.data, packet.data_length);
                    if (packet.crc != crc) {
                        corrupted_packets++;
                        continue;
                    }

                    packet_counter++;
                    if (self->player_) {
                        const uint64_t write_started = (uint64_t)esp_timer_get_time();
                        const bool wrote = self->player_->write(packet.data, packet.data_length, portMAX_DELAY);
                        const uint64_t write_elapsed = (uint64_t)esp_timer_get_time() - write_started;

                        if (!wrote) {
                            ESP_LOGW(TAG, "Audio write failed after %llu us", (unsigned long long)write_elapsed);
                        } else if (write_elapsed >= kAudioWriteBlockWarnUs) {
                            ESP_LOGW(TAG,
                                     "Audio write blocked %llu us during Himax stream",
                                     (unsigned long long)write_elapsed);
                        }
                    }

                    vTaskDelayUntil(&last_wake, kStatusPollDelay);
                }
            } break;

            case ST_IDLE:
                if (packet_counter != 0) {
                    stop = true;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
                break;

            default:
                vTaskDelay(pdMS_TO_TICKS(20));
                break;
            }

            if (abort_due_to_i2c) {
                break;
            }
        }

        if (corrupted_packets > 0) {
            ESP_LOGW(TAG, "Dropped %u corrupted audio packets", corrupted_packets);
        }

        xEventGroupClearBits(self->status_, STATUS_BUSY);

        if (abort_due_to_i2c) {
            ESP_LOGE(TAG, "Stopping TTS after I2C error");
            if (self->player_) {
                self->player_->stop();
            }
            (void)self->recoverAfterStreamError();
            audio_session_notify_error(ESP_FAIL);
            ui_notify_tts_finished();
        } else if (!paused && packet_counter > 0 && stop) {
            ui_notify_tts_finished();
        }
    }
}
