#pragma once

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_codec_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "fifo_ringbuf.h"

class AudioPlayerP4
{
public:
    using PlaybackCallback = void (*)(void *user_ctx);

    bool init(size_t frame_size);
    void deinit();
    void setPlaybackCallbacks(PlaybackCallback started_cb,
                              PlaybackCallback finished_cb,
                              void *user_ctx);
    bool write(const uint8_t *data, size_t bytes, TickType_t ticks_to_wait);
    void finishInput();
    bool isOn() const;
    bool enable();
    bool disable();
    bool pause();
    bool resume();
    bool stop();
    bool playSfxWavFile(const char *path, int volume_percent, uint32_t fade_out_delay_ms = 0, uint32_t fade_out_ms = 0);
    bool playSfxPcmFile(const char *path, int volume_percent, uint32_t fade_out_delay_ms = 0, uint32_t fade_out_ms = 0);
    bool fadeOutPrimary(uint32_t fade_out_ms);
    bool fadeOutSfx(uint32_t fade_out_ms);
    bool setVolume(int volume, int *out_volume = nullptr);
    int getVolume() const;

private:
    struct WavVoice
    {
        FILE *file = nullptr;
        uint32_t data_offset = 0;
        uint32_t data_size = 0;
        uint32_t bytes_remaining = 0;
        uint32_t samples_remaining = 0;
        uint32_t target_gain_q15 = 0;
        uint32_t fade_in_remaining = 0;
        uint32_t fade_in_total = 0;
        uint32_t fade_out_delay_remaining = 0;
        uint32_t fade_out_remaining = 0;
        uint32_t fade_out_total = 0;
        int16_t pcm_buffer[1024] = {};
        size_t pcm_pos = 0;
        size_t pcm_count = 0;
        bool active = false;
        bool loop = false;
    };

    static void controlTask(void *arg);
    static void playTask(void *arg);
    bool ensureSpeakerOpenLocked();
    bool ensureSpeakerClosedLocked();
    bool syncMuteStateLocked();
    bool syncMuteState();
    bool openWavVoiceLocked(const char *path,
                            WavVoice &voice,
                            bool loop,
                            int volume_percent,
                            uint32_t fade_in_ms,
                            uint32_t fade_out_delay_ms,
                            uint32_t fade_out_ms);
    bool openPcmVoiceLocked(const char *path,
                            WavVoice &voice,
                            bool loop,
                            int volume_percent,
                            uint32_t fade_in_ms,
                            uint32_t fade_out_delay_ms,
                            uint32_t fade_out_ms);
    bool parseWavFileLocked(FILE *file, WavVoice &voice);
    bool readVoiceSampleLocked(WavVoice &voice, int16_t *out_sample);
    bool auxActiveLocked() const;
    void closeVoiceLocked(WavVoice &voice);
    void mixAuxIntoBufferLocked(int32_t *mix_buffer, size_t sample_count);
    void updateAuxStatusFromPlayTask();

private:
    static constexpr char TAG[] = "AudioPlayerP4";
    static constexpr EventBits_t STATUS_ON = BIT0;
    static constexpr EventBits_t STATUS_PLAYING = BIT1;
    static constexpr EventBits_t STATUS_PAUSED = BIT2;
    static constexpr EventBits_t STATUS_AUX_ACTIVE = BIT3;
    static constexpr size_t kRingBufferSize = 50;
    static constexpr size_t kPlaybackStartThreshold = 20;
    static constexpr uint8_t kPlaybackStopEmptyPolls = 4;
    static constexpr size_t kMaxSfxVoices = 2;

    fifo_ringbuf_t *audio_buffer_ = nullptr;
    esp_codec_dev_handle_t speaker_ = nullptr;
    EventGroupHandle_t status_ = nullptr;
    SemaphoreHandle_t speaker_lock_ = nullptr;
    SemaphoreHandle_t mix_lock_ = nullptr;
    SemaphoreHandle_t primary_fade_lock_ = nullptr;
    TaskHandle_t control_task_ = nullptr;
    TaskHandle_t play_task_ = nullptr;
    SemaphoreHandle_t control_done_ = nullptr;
    SemaphoreHandle_t play_done_ = nullptr;
    size_t frame_size_ = 0;
    esp_codec_dev_sample_info_t speaker_format_ = {};
    WavVoice sfx_voices_[kMaxSfxVoices] = {};
    uint32_t primary_fade_remaining_ = 0;
    uint32_t primary_fade_total_ = 0;
    bool primary_fade_active_ = false;
    bool speaker_open_ = false;
    volatile bool shutting_down_ = false;
    volatile bool input_finished_ = false;
    PlaybackCallback started_cb_ = nullptr;
    PlaybackCallback finished_cb_ = nullptr;
    void *callback_ctx_ = nullptr;
    int volume_ = 30;
};
