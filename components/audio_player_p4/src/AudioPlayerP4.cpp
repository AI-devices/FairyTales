#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "audio_player_p4.h"
#include "bsp/esp-bsp.h"

namespace {
constexpr uint32_t kSampleRate = 16000;
constexpr uint8_t kBitsPerSample = 16;
constexpr uint8_t kChannels = 2;
constexpr int kVolume = 30;
constexpr int kMinVolume = 0;
constexpr int kMaxVolume = 100;
constexpr uint32_t kSfxSampleRate = 16000;
constexpr uint8_t kSfxBitsPerSample = 16;
constexpr uint8_t kSfxChannels = 1;
constexpr uint32_t kSamplesPerMs = kSfxSampleRate / 1000;
constexpr uint32_t kOneShotTailFadeStage1Samples = 560 * kSamplesPerMs;
constexpr uint32_t kOneShotTailFadeStage2Samples = 340 * kSamplesPerMs;
constexpr uint32_t kOneShotTailFadeStage3Samples = 160 * kSamplesPerMs;

static uint16_t readLe16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t readLe32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static int32_t clampPcm16(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return value;
}

static uint32_t gainFromPercent(int volume_percent)
{
    if (volume_percent < kMinVolume) {
        volume_percent = kMinVolume;
    } else if (volume_percent > kMaxVolume) {
        volume_percent = kMaxVolume;
    }

    return (uint32_t)((volume_percent * 32767) / 100);
}

static uint32_t scaleGainQ15(uint32_t gain_q15, uint32_t scale_q8)
{
    return (gain_q15 * scale_q8) / 255;
}

static uint32_t stagedTailFadeGainQ15(uint32_t gain_q15, uint32_t samples_remaining)
{
    if (samples_remaining >= kOneShotTailFadeStage1Samples) {
        return gain_q15;
    }

    if (samples_remaining >= kOneShotTailFadeStage2Samples) {
        const uint32_t span = kOneShotTailFadeStage1Samples - kOneShotTailFadeStage2Samples;
        const uint32_t done = kOneShotTailFadeStage1Samples - samples_remaining;
        const uint32_t scale_q8 = 255 - ((done * 47) / span);
        return scaleGainQ15(gain_q15, scale_q8);
    }

    if (samples_remaining >= kOneShotTailFadeStage3Samples) {
        const uint32_t span = kOneShotTailFadeStage2Samples - kOneShotTailFadeStage3Samples;
        const uint32_t done = kOneShotTailFadeStage2Samples - samples_remaining;
        const uint32_t scale_q8 = 208 - ((done * 96) / span);
        return scaleGainQ15(gain_q15, scale_q8);
    }

    return scaleGainQ15(gain_q15, (samples_remaining * 112) / kOneShotTailFadeStage3Samples);
}
}

constexpr char AudioPlayerP4::TAG[];

#if CONFIG_FAIRYTALE_VERBOSE_RUNTIME_LOGS
#define AUDIO_PLAYER_DIAG_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define AUDIO_PLAYER_DIAG_LOGI(...) do { } while (0)
#endif

bool AudioPlayerP4::init(size_t frame_size)
{
    shutting_down_ = false;
    input_finished_ = false;
    frame_size_ = frame_size;
    audio_buffer_ = fifo_ringbuf_init(kRingBufferSize, frame_size_);
    if (!audio_buffer_) {
        ESP_LOGE(TAG, "Failed to create audio ring buffer");
        return false;
    }

    speaker_ = bsp_audio_codec_speaker_init();
    if (!speaker_) {
        ESP_LOGE(TAG, "Failed to init speaker codec");
        return false;
    }

    speaker_format_ = {
        .bits_per_sample = kBitsPerSample,
        .channel = kChannels,
        .channel_mask = 0,
        .sample_rate = kSampleRate,
        .mclk_multiple = 0,
    };
    if (esp_codec_dev_open(speaker_, &speaker_format_) != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open speaker codec");
        return false;
    }
    speaker_open_ = true;

    speaker_lock_ = xSemaphoreCreateMutex();
    if (!speaker_lock_) {
        ESP_LOGE(TAG, "Failed to create speaker mutex");
        return false;
    }
    mix_lock_ = xSemaphoreCreateMutex();
    if (!mix_lock_) {
        ESP_LOGE(TAG, "Failed to create mixer mutex");
        return false;
    }
    primary_fade_lock_ = xSemaphoreCreateMutex();
    if (!primary_fade_lock_) {
        ESP_LOGE(TAG, "Failed to create primary fade mutex");
        return false;
    }

    volume_ = kVolume;
    esp_codec_dev_set_out_vol(speaker_, volume_);

    status_ = xEventGroupCreate();
    if (!status_) {
        ESP_LOGE(TAG, "Failed to create audio status bits");
        return false;
    }
    xEventGroupClearBits(status_, STATUS_ON | STATUS_PLAYING | STATUS_PAUSED | STATUS_AUX_ACTIVE);
    syncMuteState();

    control_done_ = xSemaphoreCreateBinary();
    play_done_ = xSemaphoreCreateBinary();
    if (!control_done_ || !play_done_) {
        ESP_LOGE(TAG, "Failed to create audio task completion semaphores");
        return false;
    }

    if (xTaskCreate(controlTask, "audio_ctrl_p4", 4096, this, 4, &control_task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio control task");
        return false;
    }
    if (xTaskCreate(playTask,
                    "audio_play_p4",
                    6144,
                    this,
                    configMAX_PRIORITIES - 1,
                    &play_task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio play task");
        return false;
    }

    return true;
}

void AudioPlayerP4::deinit()
{
    if (!audio_buffer_ && !status_ && !speaker_) {
        return;
    }

    shutting_down_ = true;
    if (audio_buffer_) {
        fifo_ringbuf_reset(audio_buffer_);
    }
    if (status_) {
        xEventGroupSetBits(status_, STATUS_ON | STATUS_PLAYING | STATUS_AUX_ACTIVE);
    }

    if (control_task_ && control_done_) {
        (void)xSemaphoreTake(control_done_, portMAX_DELAY);
    }
    if (play_task_ && play_done_) {
        (void)xSemaphoreTake(play_done_, portMAX_DELAY);
    }
    control_task_ = nullptr;
    play_task_ = nullptr;

    if (mix_lock_ && xSemaphoreTake(mix_lock_, portMAX_DELAY) == pdTRUE) {
        for (size_t i = 0; i < kMaxSfxVoices; ++i) {
            closeVoiceLocked(sfx_voices_[i]);
        }
        xSemaphoreGive(mix_lock_);
    }

    if (speaker_lock_ && xSemaphoreTake(speaker_lock_, portMAX_DELAY) == pdTRUE) {
        (void)ensureSpeakerClosedLocked();
        xSemaphoreGive(speaker_lock_);
    }

    if (audio_buffer_) {
        fifo_ringbuf_release(audio_buffer_);
        audio_buffer_ = nullptr;
    }
    if (status_) {
        vEventGroupDelete(status_);
        status_ = nullptr;
    }
    if (speaker_lock_) {
        vSemaphoreDelete(speaker_lock_);
        speaker_lock_ = nullptr;
    }
    if (mix_lock_) {
        vSemaphoreDelete(mix_lock_);
        mix_lock_ = nullptr;
    }
    if (primary_fade_lock_) {
        vSemaphoreDelete(primary_fade_lock_);
        primary_fade_lock_ = nullptr;
    }
    if (control_done_) {
        vSemaphoreDelete(control_done_);
        control_done_ = nullptr;
    }
    if (play_done_) {
        vSemaphoreDelete(play_done_);
        play_done_ = nullptr;
    }

    speaker_ = nullptr;
    speaker_open_ = false;
    frame_size_ = 0;
    started_cb_ = nullptr;
    finished_cb_ = nullptr;
    callback_ctx_ = nullptr;
}

void AudioPlayerP4::setPlaybackCallbacks(PlaybackCallback started_cb,
                                         PlaybackCallback finished_cb,
                                         void *user_ctx)
{
    started_cb_ = started_cb;
    finished_cb_ = finished_cb;
    callback_ctx_ = user_ctx;
}

bool AudioPlayerP4::write(const uint8_t *data, size_t bytes, TickType_t ticks_to_wait)
{
    if (!audio_buffer_ || !data || bytes == 0) {
        return false;
    }
    return fifo_ringbuf_write(audio_buffer_, data, bytes, ticks_to_wait) == bytes;
}

void AudioPlayerP4::finishInput()
{
    input_finished_ = true;
}

bool AudioPlayerP4::isOn() const
{
    return status_ && (xEventGroupGetBits(status_) & STATUS_ON);
}

bool AudioPlayerP4::enable()
{
    if (!status_ || !speaker_) {
        return false;
    }
    if (xEventGroupGetBits(status_) & STATUS_ON) {
        return true;
    }

    if (primary_fade_lock_ && xSemaphoreTake(primary_fade_lock_, portMAX_DELAY) == pdTRUE) {
        primary_fade_remaining_ = 0;
        primary_fade_total_ = 0;
        primary_fade_active_ = false;
        xSemaphoreGive(primary_fade_lock_);
    }

    xEventGroupSetBits(status_, STATUS_ON);
    syncMuteState();
    return true;
}

bool AudioPlayerP4::disable()
{
    if (!status_ || !speaker_) {
        return false;
    }
    if (!(xEventGroupGetBits(status_) & STATUS_ON)) {
        return true;
    }

    xEventGroupClearBits(status_, STATUS_ON | STATUS_PLAYING | STATUS_PAUSED);
    syncMuteState();
    return true;
}

bool AudioPlayerP4::pause()
{
    if (!status_ || !speaker_) {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(status_);
    if (!(bits & STATUS_PLAYING)) {
        return false;
    }

    xEventGroupClearBits(status_, STATUS_PLAYING);
    xEventGroupSetBits(status_, STATUS_PAUSED);
    syncMuteState();
    return true;
}

bool AudioPlayerP4::resume()
{
    if (!status_ || !speaker_) {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(status_);
    if (!(bits & STATUS_PAUSED)) {
        return false;
    }

    xEventGroupClearBits(status_, STATUS_PAUSED);
    xEventGroupSetBits(status_, STATUS_PLAYING | STATUS_ON);
    syncMuteState();
    if (started_cb_) {
        started_cb_(callback_ctx_);
    }
    return true;
}

bool AudioPlayerP4::stop()
{
    if (!audio_buffer_ || !status_) {
        return false;
    }

    fifo_ringbuf_reset(audio_buffer_);
    input_finished_ = false;
    if (primary_fade_lock_ && xSemaphoreTake(primary_fade_lock_, portMAX_DELAY) == pdTRUE) {
        primary_fade_remaining_ = 0;
        primary_fade_total_ = 0;
        primary_fade_active_ = false;
        xSemaphoreGive(primary_fade_lock_);
    }
    // SFX voices are independent of the primary stream and must finish naturally.
    disable();
    return true;
}

bool AudioPlayerP4::fadeOutPrimary(uint32_t fade_out_ms)
{
    if (!primary_fade_lock_ || !status_ || fade_out_ms == 0 ||
        !(xEventGroupGetBits(status_) & STATUS_PLAYING)) {
        return false;
    }

    if (xSemaphoreTake(primary_fade_lock_, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    primary_fade_total_ = fade_out_ms * kSamplesPerMs;
    primary_fade_remaining_ = primary_fade_total_;
    primary_fade_active_ = primary_fade_total_ > 0;
    xSemaphoreGive(primary_fade_lock_);
    return primary_fade_active_;
}

bool AudioPlayerP4::playSfxWavFile(const char *path, int volume_percent, uint32_t fade_out_delay_ms, uint32_t fade_out_ms)
{
    bool ok = false;

    if (!path || !mix_lock_ || !status_) {
        return false;
    }

    if (xSemaphoreTake(mix_lock_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (size_t i = 0; i < kMaxSfxVoices; ++i) {
        if (!sfx_voices_[i].active) {
            ok = openWavVoiceLocked(path, sfx_voices_[i], false, volume_percent, 0, fade_out_delay_ms, fade_out_ms);
            break;
        }
    }

    xSemaphoreGive(mix_lock_);

    if (!ok) {
        return false;
    }

    xEventGroupSetBits(status_, STATUS_AUX_ACTIVE);
    syncMuteState();
    return true;
}

bool AudioPlayerP4::playSfxPcmFile(const char *path, int volume_percent, uint32_t fade_out_delay_ms, uint32_t fade_out_ms)
{
    bool ok = false;

    if (!path || !mix_lock_ || !status_) {
        return false;
    }

    if (xSemaphoreTake(mix_lock_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (size_t i = 0; i < kMaxSfxVoices; ++i) {
        if (!sfx_voices_[i].active) {
            ok = openPcmVoiceLocked(path, sfx_voices_[i], false, volume_percent, 0, fade_out_delay_ms, fade_out_ms);
            break;
        }
    }

    xSemaphoreGive(mix_lock_);

    if (!ok) {
        return false;
    }

    xEventGroupSetBits(status_, STATUS_AUX_ACTIVE);
    syncMuteState();
    return true;
}

bool AudioPlayerP4::fadeOutSfx(uint32_t fade_out_ms)
{
    const uint32_t fade_samples = fade_out_ms * kSamplesPerMs;
    bool any = false;

    if (!mix_lock_) {
        return false;
    }

    if (xSemaphoreTake(mix_lock_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (size_t i = 0; i < kMaxSfxVoices; ++i) {
        WavVoice &voice = sfx_voices_[i];
        if (!voice.active) {
            continue;
        }
        any = true;
        if (fade_samples == 0) {
            closeVoiceLocked(voice);
        } else {
            voice.fade_out_total = fade_samples;
            voice.fade_out_remaining = fade_samples;
        }
    }

    xSemaphoreGive(mix_lock_);
    updateAuxStatusFromPlayTask();
    return any;
}

bool AudioPlayerP4::setVolume(int volume, int *out_volume)
{
    int clamped = volume;
    bool ok = true;

    if (clamped < kMinVolume) {
        clamped = kMinVolume;
    } else if (clamped > kMaxVolume) {
        clamped = kMaxVolume;
    }

    if (!speaker_) {
        volume_ = clamped;
        if (out_volume) {
            *out_volume = volume_;
        }
        return false;
    }

    if (!speaker_lock_ || xSemaphoreTake(speaker_lock_, portMAX_DELAY) != pdTRUE) {
        if (out_volume) {
            *out_volume = volume_;
        }
        return false;
    }

    volume_ = clamped;

    ok = ensureSpeakerOpenLocked();
    if (ok && esp_codec_dev_set_out_vol(speaker_, volume_) != ESP_CODEC_DEV_OK) {
        ok = false;
    }
    if (ok) {
        ok = syncMuteStateLocked();
    }

    xSemaphoreGive(speaker_lock_);

    if (out_volume) {
        *out_volume = volume_;
    }
    return ok;
}

int AudioPlayerP4::getVolume() const
{
    return volume_;
}

bool AudioPlayerP4::ensureSpeakerOpenLocked()
{
    if (!speaker_) {
        return false;
    }

    if (speaker_open_) {
        return true;
    }

    if (esp_codec_dev_open(speaker_, &speaker_format_) != ESP_CODEC_DEV_OK) {
        return false;
    }

    speaker_open_ = true;
    return esp_codec_dev_set_out_vol(speaker_, volume_) == ESP_CODEC_DEV_OK;
}

bool AudioPlayerP4::ensureSpeakerClosedLocked()
{
    if (!speaker_) {
        return false;
    }

    if (!speaker_open_) {
        return true;
    }

    if (esp_codec_dev_close(speaker_) != ESP_CODEC_DEV_OK) {
        return false;
    }

    speaker_open_ = false;
    return true;
}

bool AudioPlayerP4::syncMuteStateLocked()
{
    EventBits_t bits = 0;
    bool should_mute = true;

    if (!speaker_) {
        return false;
    }

    if (status_) {
        bits = xEventGroupGetBits(status_);
        const bool aux_active = (bits & STATUS_AUX_ACTIVE) != 0;
        should_mute = (((bits & STATUS_ON) == 0) && !aux_active) ||
                      (((bits & STATUS_PAUSED) != 0) && !aux_active) ||
                      volume_ <= kMinVolume;
    }

    if (!ensureSpeakerOpenLocked()) {
        return false;
    }

    if (volume_ <= kMinVolume && esp_codec_dev_set_out_vol(speaker_, 0) != ESP_CODEC_DEV_OK) {
        return false;
    }

    return esp_codec_dev_set_out_mute(speaker_, should_mute) == ESP_CODEC_DEV_OK;
}

bool AudioPlayerP4::syncMuteState()
{
    bool ok = false;

    if (!speaker_lock_ || xSemaphoreTake(speaker_lock_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    ok = syncMuteStateLocked();
    xSemaphoreGive(speaker_lock_);
    return ok;
}

void AudioPlayerP4::closeVoiceLocked(WavVoice &voice)
{
    if (voice.file) {
        fclose(voice.file);
    }
    voice = {};
}

bool AudioPlayerP4::parseWavFileLocked(FILE *file, WavVoice &voice)
{
    uint8_t header[12] = {0};
    bool fmt_found = false;
    bool data_found = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    if (!file || fread(header, 1, sizeof(header), file) != sizeof(header)) {
        return false;
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }

    while (!data_found) {
        uint8_t chunk_header[8] = {0};
        uint32_t chunk_size = 0;
        long next_chunk = 0;

        if (fread(chunk_header, 1, sizeof(chunk_header), file) != sizeof(chunk_header)) {
            break;
        }

        chunk_size = readLe32(chunk_header + 4);
        next_chunk = ftell(file);
        if (next_chunk < 0) {
            return false;
        }
        next_chunk += (long)chunk_size + (long)(chunk_size & 1U);

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {0};
            if (chunk_size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) {
                return false;
            }
            audio_format = readLe16(fmt);
            channels = readLe16(fmt + 2);
            sample_rate = readLe32(fmt + 4);
            bits_per_sample = readLe16(fmt + 14);
            fmt_found = true;
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            long data_offset = ftell(file);
            if (data_offset < 0 || chunk_size == 0) {
                return false;
            }
            voice.data_offset = (uint32_t)data_offset;
            voice.data_size = chunk_size;
            voice.bytes_remaining = chunk_size;
            voice.samples_remaining = chunk_size / sizeof(int16_t);
            data_found = true;
        }

        if (fseek(file, next_chunk, SEEK_SET) != 0) {
            return false;
        }
    }

    if (!fmt_found ||
        !data_found ||
        audio_format != 1 ||
        channels != kSfxChannels ||
        sample_rate != kSfxSampleRate ||
        bits_per_sample != kSfxBitsPerSample) {
        ESP_LOGW(TAG,
                 "unsupported WAV format: pcm=%u channels=%u rate=%u bits=%u",
                 (unsigned)audio_format,
                 (unsigned)channels,
                 (unsigned)sample_rate,
                 (unsigned)bits_per_sample);
        return false;
    }

    return fseek(file, (long)voice.data_offset, SEEK_SET) == 0;
}

bool AudioPlayerP4::openWavVoiceLocked(const char *path,
                                       WavVoice &voice,
                                       bool loop,
                                       int volume_percent,
                                       uint32_t fade_in_ms,
                                       uint32_t fade_out_delay_ms,
                                       uint32_t fade_out_ms)
{
    FILE *file = nullptr;
    WavVoice candidate = {};
    const uint32_t fade_samples = fade_in_ms * kSamplesPerMs;

    if (!path) {
        return false;
    }

    file = fopen(path, "rb");
    if (!file) {
        ESP_LOGW(TAG, "SFX WAV not found: %s", path);
        return false;
    }

    candidate.file = file;
    candidate.loop = loop;
    candidate.target_gain_q15 = gainFromPercent(volume_percent);
    candidate.fade_in_total = fade_samples;
    candidate.fade_in_remaining = fade_samples;
    candidate.fade_out_delay_remaining = fade_out_delay_ms * kSamplesPerMs;
    candidate.fade_out_total = fade_out_ms * kSamplesPerMs;

    if (!parseWavFileLocked(file, candidate)) {
        fclose(file);
        ESP_LOGW(TAG, "unable to use WAV file: %s", path);
        return false;
    }

    closeVoiceLocked(voice);
    voice = candidate;
    voice.active = true;
    return true;
}

bool AudioPlayerP4::openPcmVoiceLocked(const char *path,
                                       WavVoice &voice,
                                       bool loop,
                                       int volume_percent,
                                       uint32_t fade_in_ms,
                                       uint32_t fade_out_delay_ms,
                                       uint32_t fade_out_ms)
{
    FILE *file = nullptr;
    WavVoice candidate = {};
    const uint32_t fade_samples = fade_in_ms * kSamplesPerMs;

    if (!path) {
        return false;
    }

    file = fopen(path, "rb");
    if (!file) {
        ESP_LOGW(TAG, "SFX PCM not found: %s", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }

    const long file_size = ftell(file);
    if (file_size <= 0 || (file_size % (long)sizeof(int16_t)) != 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        ESP_LOGW(TAG, "invalid PCM16 file: %s", path);
        return false;
    }

    candidate.file = file;
    candidate.data_offset = 0;
    candidate.data_size = (uint32_t)file_size;
    candidate.bytes_remaining = (uint32_t)file_size;
    candidate.samples_remaining = (uint32_t)file_size / sizeof(int16_t);
    candidate.loop = loop;
    candidate.target_gain_q15 = gainFromPercent(volume_percent);
    candidate.fade_in_total = fade_samples;
    candidate.fade_in_remaining = fade_samples;
    candidate.fade_out_delay_remaining = fade_out_delay_ms * kSamplesPerMs;
    candidate.fade_out_total = fade_out_ms * kSamplesPerMs;

    closeVoiceLocked(voice);
    voice = candidate;
    voice.active = true;
    return true;
}

bool AudioPlayerP4::readVoiceSampleLocked(WavVoice &voice, int16_t *out_sample)
{
    if (out_sample) {
        *out_sample = 0;
    }

    while (voice.active) {
        if (voice.pcm_pos < voice.pcm_count) {
            if (out_sample) {
                *out_sample = voice.pcm_buffer[voice.pcm_pos];
            }
            voice.pcm_pos++;
            if (voice.samples_remaining > 0) {
                voice.samples_remaining--;
            }
            return true;
        }

        if (voice.bytes_remaining < sizeof(int16_t)) {
            if (!voice.loop) {
                closeVoiceLocked(voice);
                return false;
            }
            if (fseek(voice.file, (long)voice.data_offset, SEEK_SET) != 0) {
                closeVoiceLocked(voice);
                return false;
            }
            voice.bytes_remaining = voice.data_size;
            voice.samples_remaining = voice.data_size / sizeof(int16_t);
            voice.pcm_pos = 0;
            voice.pcm_count = 0;
            continue;
        }

        size_t bytes_to_read = voice.bytes_remaining;
        if (bytes_to_read > sizeof(voice.pcm_buffer)) {
            bytes_to_read = sizeof(voice.pcm_buffer);
        }
        bytes_to_read -= bytes_to_read % sizeof(int16_t);

        const size_t bytes_read = fread(voice.pcm_buffer, 1, bytes_to_read, voice.file);
        if (bytes_read < sizeof(int16_t)) {
            closeVoiceLocked(voice);
            return false;
        }

        voice.bytes_remaining -= (uint32_t)(bytes_read - (bytes_read % sizeof(int16_t)));
        voice.pcm_pos = 0;
        voice.pcm_count = bytes_read / sizeof(int16_t);
    }

    return false;
}

bool AudioPlayerP4::auxActiveLocked() const
{
    for (size_t i = 0; i < kMaxSfxVoices; ++i) {
        if (sfx_voices_[i].active) {
            return true;
        }
    }

    return false;
}

void AudioPlayerP4::mixAuxIntoBufferLocked(int32_t *mix_buffer, size_t sample_count)
{
    if (!mix_buffer || sample_count == 0) {
        return;
    }

    auto mix_voice = [&](WavVoice &voice) {
        for (size_t i = 0; i < sample_count && voice.active; ++i) {
            int16_t sample = 0;
            uint32_t gain_q15 = voice.target_gain_q15;

            if (!readVoiceSampleLocked(voice, &sample)) {
                break;
            }

            if (voice.fade_in_remaining > 0 && voice.fade_in_total > 0) {
                const uint32_t done = voice.fade_in_total - voice.fade_in_remaining;
                gain_q15 = (gain_q15 * done) / voice.fade_in_total;
                voice.fade_in_remaining--;
            }

            if (voice.fade_out_remaining > 0 && voice.fade_out_total > 0) {
                gain_q15 = (gain_q15 * voice.fade_out_remaining) / voice.fade_out_total;
                voice.fade_out_remaining--;
                if (voice.fade_out_remaining == 0) {
                    closeVoiceLocked(voice);
                    break;
                }
            } else if (voice.fade_out_delay_remaining > 0) {
                voice.fade_out_delay_remaining--;
                if (voice.fade_out_delay_remaining == 0 && voice.fade_out_total > 0) {
                    voice.fade_out_remaining = voice.fade_out_total;
                }
            }

            gain_q15 = stagedTailFadeGainQ15(gain_q15, voice.samples_remaining);
            mix_buffer[i] += (int32_t)(((int32_t)sample * (int32_t)gain_q15) >> 15);
        }
    };

    for (size_t i = 0; i < kMaxSfxVoices; ++i) {
        mix_voice(sfx_voices_[i]);
    }
}

void AudioPlayerP4::updateAuxStatusFromPlayTask()
{
    bool active = false;

    if (!status_ || !mix_lock_) {
        return;
    }

    if (xSemaphoreTake(mix_lock_, portMAX_DELAY) != pdTRUE) {
        return;
    }
    active = auxActiveLocked();
    xSemaphoreGive(mix_lock_);

    if (active) {
        xEventGroupSetBits(status_, STATUS_AUX_ACTIVE);
    } else {
        xEventGroupClearBits(status_, STATUS_AUX_ACTIVE);
    }
    syncMuteState();
}

void AudioPlayerP4::controlTask(void *arg)
{
    auto *player = static_cast<AudioPlayerP4 *>(arg);
    uint8_t empty_polls = 0;

    while (!player->shutting_down_) {
        EventBits_t bits = xEventGroupGetBits(player->status_);

        if (!(bits & STATUS_ON)) {
            empty_polls = 0;
            const size_t buffered = fifo_ringbuf_size(player->audio_buffer_);
            if (buffered > kPlaybackStartThreshold || (player->input_finished_ && buffered > 0)) {
                ESP_LOGI(TAG, "Starting playback");
                AUDIO_PLAYER_DIAG_LOGI("bird_timing stage=audio_player_start_playback ts_us=%llu buffered=%u threshold=%u",
                                       (unsigned long long)esp_timer_get_time(),
                                       (unsigned)fifo_ringbuf_size(player->audio_buffer_),
                                       (unsigned)kPlaybackStartThreshold);
                player->enable();
                xEventGroupClearBits(player->status_, STATUS_PAUSED);
                xEventGroupSetBits(player->status_, STATUS_PLAYING);
                if (player->started_cb_) {
                    player->started_cb_(player->callback_ctx_);
                }
            }
        } else if (bits & STATUS_PAUSED) {
            empty_polls = 0;
        } else if (player->input_finished_ && fifo_ringbuf_empty(player->audio_buffer_)) {
            if (empty_polls < kPlaybackStopEmptyPolls) {
                empty_polls++;
            }
            if (empty_polls >= kPlaybackStopEmptyPolls) {
                ESP_LOGI(TAG, "Stopping playback");
                player->disable();
                if (player->finished_cb_) {
                    player->finished_cb_(player->callback_ctx_);
                }
                empty_polls = 0;
            }
        } else {
            empty_polls = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (player->control_done_) {
        xSemaphoreGive(player->control_done_);
    }
    vTaskDelete(nullptr);
}

void AudioPlayerP4::playTask(void *arg)
{
    auto *player = static_cast<AudioPlayerP4 *>(arg);
    auto *mono_buffer = static_cast<uint8_t *>(malloc(player->frame_size_));
    auto *stereo_buffer = static_cast<uint8_t *>(malloc(player->frame_size_ * 2));
    const size_t frame_samples = player->frame_size_ / sizeof(int16_t);
    auto *mix_buffer = static_cast<int32_t *>(calloc(frame_samples, sizeof(int32_t)));
    uint32_t narration_underruns = 0;

    if (!mono_buffer || !stereo_buffer || !mix_buffer) {
        ESP_LOGE(TAG, "Failed to allocate playback buffers");
        free(mono_buffer);
        free(stereo_buffer);
        free(mix_buffer);
        if (player->play_done_) {
            xSemaphoreGive(player->play_done_);
        }
        vTaskDelete(nullptr);
        return;
    }

    while (!player->shutting_down_) {
        EventBits_t bits = xEventGroupGetBits(player->status_);
        bool narration_active = (bits & STATUS_PLAYING) != 0;
        bool aux_active = (bits & STATUS_AUX_ACTIVE) != 0;
        bool had_aux_bit = aux_active;
        bool has_output = false;
        size_t output_samples = frame_samples;
        size_t narration_samples = 0;
        int16_t *narration_src = nullptr;

        if (!narration_active && !aux_active) {
            bits = xEventGroupWaitBits(player->status_,
                                       STATUS_PLAYING | STATUS_AUX_ACTIVE,
                                       pdFALSE,
                                       pdFALSE,
                                       pdMS_TO_TICKS(100));
            narration_active = (bits & STATUS_PLAYING) != 0;
            aux_active = (bits & STATUS_AUX_ACTIVE) != 0;
            had_aux_bit = aux_active;
            if (!narration_active && !aux_active) {
                continue;
            }
        }

        memset(mix_buffer, 0, frame_samples * sizeof(int32_t));

        if (narration_active) {
            size_t read = fifo_ringbuf_read(player->audio_buffer_,
                                            mono_buffer,
                                            player->frame_size_,
                                            pdMS_TO_TICKS(100));
            narration_samples = read / sizeof(int16_t);
            if (read == 0 && !player->input_finished_) {
                ++narration_underruns;
                if (narration_underruns == 1 || (narration_underruns % 10) == 0) {
                    ESP_LOGW(TAG,
                             "Narration buffer underrun count=%lu",
                             (unsigned long)narration_underruns);
                }
            }
            narration_src = reinterpret_cast<int16_t *>(mono_buffer);
            has_output = narration_samples > 0;
            if (!had_aux_bit) {
                output_samples = narration_samples;
            }
        }

        if (player->primary_fade_lock_ &&
            xSemaphoreTake(player->primary_fade_lock_, portMAX_DELAY) == pdTRUE) {
            for (size_t i = 0; i < narration_samples && i < frame_samples; ++i) {
                uint32_t gain_q15 = 32767U;
                if (player->primary_fade_active_) {
                    if (player->primary_fade_remaining_ == 0 || player->primary_fade_total_ == 0) {
                        gain_q15 = 0;
                    } else {
                        gain_q15 = (32767U * player->primary_fade_remaining_) /
                                   player->primary_fade_total_;
                        player->primary_fade_remaining_--;
                    }
                }
                mix_buffer[i] += (int32_t)(((int32_t)narration_src[i] * (int32_t)gain_q15) >> 15);
            }
            xSemaphoreGive(player->primary_fade_lock_);
        } else {
            for (size_t i = 0; i < narration_samples && i < frame_samples; ++i) {
                mix_buffer[i] += narration_src[i];
            }
        }

        if (aux_active && player->mix_lock_ &&
            xSemaphoreTake(player->mix_lock_, portMAX_DELAY) == pdTRUE) {
            player->mixAuxIntoBufferLocked(mix_buffer, frame_samples);
            aux_active = player->auxActiveLocked();
            xSemaphoreGive(player->mix_lock_);
        }

        if (had_aux_bit) {
            has_output = has_output || aux_active || had_aux_bit;
        }

        if (aux_active != had_aux_bit) {
            if (aux_active) {
                xEventGroupSetBits(player->status_, STATUS_AUX_ACTIVE);
            } else {
                xEventGroupClearBits(player->status_, STATUS_AUX_ACTIVE);
            }
            player->syncMuteState();
        }

        if (!has_output && !had_aux_bit && !aux_active) {
            continue;
        }
        if (!has_output && had_aux_bit && !aux_active) {
            continue;
        }

        auto *dst = reinterpret_cast<int16_t *>(stereo_buffer);
        for (size_t i = 0; i < output_samples; ++i) {
            const int16_t sample = (int16_t)clampPcm16(mix_buffer[i]);
            dst[2 * i] = sample;
            dst[2 * i + 1] = sample;
        }

        size_t stereo_bytes = output_samples * sizeof(int16_t) * 2;
        if (stereo_bytes > 0) {
            if (player->speaker_lock_ && xSemaphoreTake(player->speaker_lock_, portMAX_DELAY) == pdTRUE) {
                if (player->speaker_open_) {
                    if (player->volume_ <= kMinVolume) {
                        memset(stereo_buffer, 0, stereo_bytes);
                    }
                    esp_codec_dev_write(player->speaker_, stereo_buffer, static_cast<int>(stereo_bytes));
                }
                xSemaphoreGive(player->speaker_lock_);
            }
        }
    }

    free(mono_buffer);
    free(stereo_buffer);
    free(mix_buffer);
    if (player->play_done_) {
        xSemaphoreGive(player->play_done_);
    }
    vTaskDelete(nullptr);
}
