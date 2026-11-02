#include "audio_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_player_p4.h"
#include "media_storage.h"
#include "mp3dec.h"

namespace {

constexpr size_t kPcmFrameBytes = 1152 * sizeof(int16_t);
constexpr size_t kMp3InputBytes = 4096;
constexpr uint32_t kRequiredSampleRate = 16000;
constexpr size_t kMp3InitialCacheSamples = 4 * kRequiredSampleRate;
constexpr size_t kMaxMp3CacheEntries = 12;
constexpr uint16_t kRequiredChannels = 1;
constexpr uint16_t kRequiredBits = 16;

enum class CommandType : uint8_t
{
    PlayPrimary = 0,
    Shutdown,
};

struct Command
{
    CommandType type;
    uint32_t session_id;
    uint32_t generation;
    char *uri;
};

struct Mp3CacheEntry
{
    char *uri = nullptr;
    int16_t *pcm = nullptr;
    size_t sample_count = 0;
};

struct AudioSessionState
{
    AudioPlayerP4 player;
    QueueHandle_t command_queue = nullptr;
    SemaphoreHandle_t lock = nullptr;
    SemaphoreHandle_t playback_done = nullptr;
    SemaphoreHandle_t worker_done = nullptr;
    TaskHandle_t worker = nullptr;
    audio_session_event_cb_t callback = nullptr;
    void *callback_ctx = nullptr;
    uint32_t next_session_id = 1;
    uint32_t active_session_id = 0;
    volatile uint32_t generation = 1;
    esp_err_t last_error = ESP_OK;
    bool initialized = false;
    bool active = false;
    bool paused = false;
    Mp3CacheEntry mp3_cache[kMaxMp3CacheEntries] = {};
};

static const char *TAG = "audio_session";
static AudioSessionState s_state = {};

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool generation_is_current(uint32_t generation)
{
    return s_state.initialized && s_state.generation == generation;
}

static void emit_event(audio_session_event_t event, uint32_t session_id, esp_err_t error)
{
    audio_session_event_cb_t callback = nullptr;
    void *callback_ctx = nullptr;

    if (s_state.lock && xSemaphoreTake(s_state.lock, portMAX_DELAY) == pdTRUE) {
        callback = s_state.callback;
        callback_ctx = s_state.callback_ctx;
        xSemaphoreGive(s_state.lock);
    }

    if (callback) {
        const audio_session_event_ctx_t ctx = {
            .session_id = session_id,
            .event = event,
            .error = error,
        };
        callback(&ctx, callback_ctx);
    }
}

static void update_state(uint32_t session_id, bool active, bool paused, esp_err_t error)
{
    if (!s_state.lock || xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_state.active_session_id = session_id;
    s_state.active = active;
    s_state.paused = paused;
    s_state.last_error = error;
    xSemaphoreGive(s_state.lock);
}

static esp_err_t resolve_audio_uri(const char *uri, char *out_path, size_t out_path_size)
{
    ESP_RETURN_ON_FALSE(uri && out_path && out_path_size > 0,
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "invalid audio URI");
    ESP_RETURN_ON_ERROR(media_storage_ensure_ready_for_uri(uri), TAG, "audio storage not ready");
    ESP_RETURN_ON_ERROR(media_storage_resolve_path(uri, out_path, out_path_size), TAG, "audio URI resolve failed");
    return ESP_OK;
}

static esp_err_t write_pcm(const int16_t *pcm, size_t sample_count, uint32_t generation)
{
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(pcm);
    size_t remaining = sample_count * sizeof(int16_t);

    while (remaining > 0 && generation_is_current(generation)) {
        const size_t chunk = remaining > kPcmFrameBytes ? kPcmFrameBytes : remaining;
        if (!s_state.player.write(bytes, chunk, pdMS_TO_TICKS(50))) {
            continue;
        }
        bytes += chunk;
        remaining -= chunk;
    }

    return generation_is_current(generation) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static int16_t *allocate_mp3_cache(size_t sample_capacity)
{
    const size_t bytes = sample_capacity * sizeof(int16_t);
    int16_t *cache = static_cast<int16_t *>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!cache) {
        cache = static_cast<int16_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    return cache;
}

static bool append_mp3_pcm(int16_t **cache,
                           size_t *sample_count,
                           size_t *sample_capacity,
                           const int16_t *pcm,
                           size_t pcm_samples)
{
    if (!cache || !sample_count || !sample_capacity || !pcm || pcm_samples == 0) {
        return false;
    }

    const size_t required = *sample_count + pcm_samples;
    if (required > *sample_capacity) {
        size_t new_capacity = *sample_capacity ? *sample_capacity : kMp3InitialCacheSamples;
        while (new_capacity < required) {
            new_capacity *= 2;
        }

        int16_t *new_cache = allocate_mp3_cache(new_capacity);
        if (!new_cache) {
            return false;
        }
        if (*cache && *sample_count > 0) {
            memcpy(new_cache, *cache, *sample_count * sizeof(int16_t));
        }
        heap_caps_free(*cache);
        *cache = new_cache;
        *sample_capacity = new_capacity;
    }

    memcpy(*cache + *sample_count, pcm, pcm_samples * sizeof(int16_t));
    *sample_count += pcm_samples;
    return true;
}

static esp_err_t decode_mp3_to_memory(FILE *file,
                                      uint32_t generation,
                                      bool cancellable,
                                      int16_t **out_pcm,
                                      size_t *out_sample_count)
{
    HMP3Decoder decoder = MP3InitDecoder();
    uint8_t *input = static_cast<uint8_t *>(
        heap_caps_calloc(kMp3InputBytes, 1, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    int16_t *pcm = static_cast<int16_t *>(
        heap_caps_calloc(MAX_NGRAN * MAX_NSAMP * MAX_NCHAN,
                         sizeof(int16_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    uint8_t *read_ptr = input;
    int bytes_left = 0;
    bool eof = false;
    int16_t *decoded_pcm = allocate_mp3_cache(kMp3InitialCacheSamples);
    size_t decoded_samples = 0;
    size_t decoded_capacity = decoded_pcm ? kMp3InitialCacheSamples : 0;
    esp_err_t result = ESP_OK;

    if (!file || !out_pcm || !out_sample_count) {
        result = ESP_ERR_INVALID_ARG;
    } else if (!decoder || !input || !pcm || !decoded_pcm) {
        result = ESP_ERR_NO_MEM;
    }

    if (result != ESP_OK) {
        if (decoded_pcm) {
            heap_caps_free(decoded_pcm);
        }
        if (pcm) {
            heap_caps_free(pcm);
        }
        if (input) {
            heap_caps_free(input);
        }
        if (decoder) {
            MP3FreeDecoder(decoder);
        }
        return result;
    }

    while (!cancellable || generation_is_current(generation)) {
        if (bytes_left < (2 * MAINBUF_SIZE) && !eof) {
            if (bytes_left > 0 && read_ptr != input) {
                memmove(input, read_ptr, (size_t)bytes_left);
            }
            const size_t read_now = fread(input + bytes_left,
                                          1,
                                          kMp3InputBytes - (size_t)bytes_left,
                                          file);
            bytes_left += (int)read_now;
            read_ptr = input;
            eof = read_now == 0;
        }

        const int sync_offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (sync_offset < 0) {
            if (eof) {
                break;
            }
            bytes_left = 0;
            read_ptr = input;
            continue;
        }

        read_ptr += sync_offset;
        bytes_left -= sync_offset;

        const int decode_error = MP3Decode(decoder, &read_ptr, &bytes_left, pcm, 0);
        if (decode_error == ERR_MP3_INDATA_UNDERFLOW) {
            if (eof) {
                break;
            }
            continue;
        }
        if (decode_error == ERR_MP3_MAINDATA_UNDERFLOW) {
            continue;
        }
        if (decode_error != ERR_MP3_NONE) {
            ESP_LOGE(TAG, "MP3 decode failed: %d", decode_error);
            result = ESP_FAIL;
            break;
        }

        MP3FrameInfo info = {};
        MP3GetLastFrameInfo(decoder, &info);
        if (info.samprate != (int)kRequiredSampleRate ||
            info.nChans != (int)kRequiredChannels ||
            info.bitsPerSample != (int)kRequiredBits) {
            ESP_LOGE(TAG,
                     "unsupported MP3 format: rate=%d channels=%d bits=%d",
                     info.samprate,
                     info.nChans,
                     info.bitsPerSample);
            result = ESP_ERR_NOT_SUPPORTED;
            break;
        }

        if (!append_mp3_pcm(&decoded_pcm,
                            &decoded_samples,
                            &decoded_capacity,
                            pcm,
                            (size_t)info.outputSamps)) {
            result = ESP_ERR_NO_MEM;
            break;
        }
    }

    heap_caps_free(pcm);
    heap_caps_free(input);
    MP3FreeDecoder(decoder);
    if (cancellable && !generation_is_current(generation)) {
        result = ESP_ERR_INVALID_STATE;
    }
    if (result == ESP_OK && decoded_samples == 0) {
        result = ESP_ERR_INVALID_SIZE;
    }
    if (result != ESP_OK) {
        heap_caps_free(decoded_pcm);
        return result;
    }

    if (decoded_capacity > decoded_samples) {
        int16_t *exact_pcm = allocate_mp3_cache(decoded_samples);
        if (exact_pcm) {
            memcpy(exact_pcm, decoded_pcm, decoded_samples * sizeof(int16_t));
            heap_caps_free(decoded_pcm);
            decoded_pcm = exact_pcm;
        }
    }

    *out_pcm = decoded_pcm;
    *out_sample_count = decoded_samples;
    return ESP_OK;
}

static esp_err_t decode_mp3(FILE *file, uint32_t generation)
{
    int16_t *decoded_pcm = nullptr;
    size_t decoded_samples = 0;
    esp_err_t result = decode_mp3_to_memory(file,
                                            generation,
                                            true,
                                            &decoded_pcm,
                                            &decoded_samples);
    if (result == ESP_OK) {
        result = write_pcm(decoded_pcm, decoded_samples, generation);
    }
    heap_caps_free(decoded_pcm);
    return result;
}

static bool find_cached_mp3(const char *uri, const int16_t **out_pcm, size_t *out_sample_count)
{
    bool found = false;

    if (!uri || !s_state.lock ||
        xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    for (size_t i = 0; i < kMaxMp3CacheEntries; ++i) {
        const Mp3CacheEntry &entry = s_state.mp3_cache[i];
        if (entry.uri && strcmp(entry.uri, uri) == 0 && entry.pcm && entry.sample_count > 0) {
            if (out_pcm) {
                *out_pcm = entry.pcm;
            }
            if (out_sample_count) {
                *out_sample_count = entry.sample_count;
            }
            found = true;
            break;
        }
    }

    xSemaphoreGive(s_state.lock);
    return found;
}

static esp_err_t decode_wav(FILE *file, uint32_t generation)
{
    uint8_t header[12] = {};
    bool fmt_found = false;
    bool data_found = false;
    uint32_t data_bytes = 0;
    int16_t pcm[kPcmFrameBytes / sizeof(int16_t)] = {};

    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (!data_found) {
        uint8_t chunk_header[8] = {};
        if (fread(chunk_header, 1, sizeof(chunk_header), file) != sizeof(chunk_header)) {
            return ESP_ERR_INVALID_RESPONSE;
        }

        const uint32_t chunk_size = read_le32(chunk_header + 4);
        const long chunk_data = ftell(file);
        if (chunk_data < 0) {
            return ESP_FAIL;
        }

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            uint8_t fmt[16] = {};
            if (chunk_size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            if (read_le16(fmt) != 1 ||
                read_le16(fmt + 2) != kRequiredChannels ||
                read_le32(fmt + 4) != kRequiredSampleRate ||
                read_le16(fmt + 14) != kRequiredBits) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            fmt_found = true;
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            data_bytes = chunk_size;
            data_found = true;
            break;
        }

        if (fseek(file, chunk_data + (long)chunk_size + (long)(chunk_size & 1U), SEEK_SET) != 0) {
            return ESP_FAIL;
        }
    }

    if (!fmt_found || data_bytes == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (data_bytes > 0 && generation_is_current(generation)) {
        size_t read_bytes = data_bytes > sizeof(pcm) ? sizeof(pcm) : data_bytes;
        read_bytes -= read_bytes % sizeof(int16_t);
        if (read_bytes == 0 || fread(pcm, 1, read_bytes, file) != read_bytes) {
            return ESP_FAIL;
        }
        ESP_RETURN_ON_ERROR(write_pcm(pcm, read_bytes / sizeof(int16_t), generation),
                            TAG,
                            "WAV playback cancelled");
        data_bytes -= (uint32_t)read_bytes;
    }

    return generation_is_current(generation) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t decode_pcm(FILE *file, uint32_t generation)
{
    int16_t pcm[kPcmFrameBytes / sizeof(int16_t)] = {};
    bool wrote_pcm = false;

    while (generation_is_current(generation)) {
        const size_t read_bytes = fread(pcm, 1, sizeof(pcm), file);
        if (read_bytes > 0) {
            if ((read_bytes % sizeof(int16_t)) != 0) {
                return ESP_ERR_INVALID_SIZE;
            }
            ESP_RETURN_ON_ERROR(write_pcm(pcm, read_bytes / sizeof(int16_t), generation),
                                TAG,
                                "PCM playback cancelled");
            wrote_pcm = true;
        }
        if (read_bytes < sizeof(pcm)) {
            if (ferror(file)) {
                return ESP_FAIL;
            }
            break;
        }
    }

    if (!generation_is_current(generation)) {
        return ESP_ERR_INVALID_STATE;
    }
    return wrote_pcm ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t decode_primary_uri(const char *uri, uint32_t generation)
{
    char path[256] = {};
    FILE *file = nullptr;
    const int16_t *cached_pcm = nullptr;
    size_t cached_samples = 0;

    if (find_cached_mp3(uri, &cached_pcm, &cached_samples)) {
        return write_pcm(cached_pcm, cached_samples, generation);
    }

    esp_err_t result = resolve_audio_uri(uri, path, sizeof(path));
    if (result != ESP_OK) {
        return result;
    }

    file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }

    const char *extension = strrchr(path, '.');
    if (extension && strcasecmp(extension, ".mp3") == 0) {
        result = decode_mp3(file, generation);
    } else if (extension && strcasecmp(extension, ".wav") == 0) {
        result = decode_wav(file, generation);
    } else if (extension && strcasecmp(extension, ".pcm") == 0) {
        result = decode_pcm(file, generation);
    } else {
        result = ESP_ERR_NOT_SUPPORTED;
    }

    fclose(file);
    return result;
}

static void player_started(void *)
{
    uint32_t session_id = 0;
    if (s_state.lock && xSemaphoreTake(s_state.lock, portMAX_DELAY) == pdTRUE) {
        session_id = s_state.active_session_id;
        s_state.active = session_id != 0;
        s_state.paused = false;
        xSemaphoreGive(s_state.lock);
    }
    if (session_id != 0) {
        emit_event(AUDIO_SESSION_EVENT_PLAYING, session_id, ESP_OK);
    }
}

static void player_finished(void *)
{
    if (s_state.playback_done) {
        xSemaphoreGive(s_state.playback_done);
    }
}

static void handle_primary(const Command &cmd)
{
    if (!generation_is_current(cmd.generation)) {
        return;
    }

    while (xSemaphoreTake(s_state.playback_done, 0) == pdTRUE) {
    }

    update_state(cmd.session_id, true, false, ESP_OK);
    emit_event(AUDIO_SESSION_EVENT_BUFFERING, cmd.session_id, ESP_OK);

    const esp_err_t result = decode_primary_uri(cmd.uri, cmd.generation);
    if (!generation_is_current(cmd.generation)) {
        return;
    }
    if (result != ESP_OK) {
        (void)s_state.player.stop();
        update_state(0, false, false, result);
        emit_event(AUDIO_SESSION_EVENT_ERROR, cmd.session_id, result);
        return;
    }

    s_state.player.finishInput();
    if (xSemaphoreTake(s_state.playback_done, portMAX_DELAY) != pdTRUE ||
        !generation_is_current(cmd.generation)) {
        return;
    }

    update_state(0, false, false, ESP_OK);
    emit_event(AUDIO_SESSION_EVENT_FINISHED, cmd.session_id, ESP_OK);
}

static void worker_task(void *)
{
    for (;;) {
        Command cmd = {};
        if (xQueueReceive(s_state.command_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (cmd.type == CommandType::Shutdown) {
            free(cmd.uri);
            break;
        }
        if (cmd.type == CommandType::PlayPrimary) {
            handle_primary(cmd);
        }

        free(cmd.uri);
    }

    if (s_state.worker_done) {
        xSemaphoreGive(s_state.worker_done);
    }
    vTaskDelete(nullptr);
}

static esp_err_t enqueue_primary(const char *uri, uint32_t session_id, uint32_t generation)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    ESP_RETURN_ON_FALSE(uri && uri[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "audio URI is empty");

    Command cmd = {
        .type = CommandType::PlayPrimary,
        .session_id = session_id,
        .generation = generation,
        .uri = strdup(uri),
    };
    if (!cmd.uri) {
        return ESP_ERR_NO_MEM;
    }
    if (xQueueSend(s_state.command_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        free(cmd.uri);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

} // namespace

extern "C" esp_err_t audio_session_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    s_state.lock = xSemaphoreCreateMutex();
    s_state.playback_done = xSemaphoreCreateBinary();
    s_state.worker_done = xSemaphoreCreateBinary();
    s_state.command_queue = xQueueCreate(8, sizeof(Command));
    if (!s_state.lock || !s_state.playback_done || !s_state.worker_done || !s_state.command_queue) {
        audio_session_deinit();
        return ESP_ERR_NO_MEM;
    }

    if (!s_state.player.init(kPcmFrameBytes)) {
        audio_session_deinit();
        return ESP_FAIL;
    }
    s_state.player.setPlaybackCallbacks(player_started, player_finished, nullptr);

    s_state.initialized = true;
    if (xTaskCreate(worker_task,
                    "audio_files",
                    12288,
                    nullptr,
                    configMAX_PRIORITIES - 2,
                    &s_state.worker) != pdPASS) {
        audio_session_deinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "local file audio session initialized");
    return ESP_OK;
}

extern "C" void audio_session_deinit(void)
{
    if (s_state.initialized) {
        (void)audio_session_stop();
        Command shutdown = {.type = CommandType::Shutdown};
        (void)xQueueSend(s_state.command_queue, &shutdown, portMAX_DELAY);
        if (s_state.worker && s_state.worker_done) {
            (void)xSemaphoreTake(s_state.worker_done, portMAX_DELAY);
        }
    }

    s_state.initialized = false;
    s_state.worker = nullptr;
    s_state.player.deinit();

    for (size_t i = 0; i < kMaxMp3CacheEntries; ++i) {
        free(s_state.mp3_cache[i].uri);
        heap_caps_free(s_state.mp3_cache[i].pcm);
        s_state.mp3_cache[i] = {};
    }

    if (s_state.command_queue) {
        Command pending = {};
        while (xQueueReceive(s_state.command_queue, &pending, 0) == pdTRUE) {
            free(pending.uri);
        }
        vQueueDelete(s_state.command_queue);
    }
    if (s_state.playback_done) {
        vSemaphoreDelete(s_state.playback_done);
    }
    if (s_state.worker_done) {
        vSemaphoreDelete(s_state.worker_done);
    }
    if (s_state.lock) {
        vSemaphoreDelete(s_state.lock);
    }

    s_state = {};
}

extern "C" esp_err_t audio_session_preload_uri(const char *uri)
{
    char path[256] = {};
    FILE *file = nullptr;
    int16_t *decoded_pcm = nullptr;
    size_t decoded_samples = 0;
    esp_err_t result = ESP_OK;

    ESP_RETURN_ON_FALSE(s_state.initialized,
                        ESP_ERR_INVALID_STATE,
                        TAG,
                        "audio session not initialized");
    ESP_RETURN_ON_FALSE(uri && uri[0] != '\0',
                        ESP_ERR_INVALID_ARG,
                        TAG,
                        "audio URI is empty");

    if (find_cached_mp3(uri, nullptr, nullptr)) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(resolve_audio_uri(uri, path, sizeof(path)),
                        TAG,
                        "audio preload URI resolve failed");
    const char *extension = strrchr(path, '.');
    ESP_RETURN_ON_FALSE(extension && strcasecmp(extension, ".mp3") == 0,
                        ESP_ERR_NOT_SUPPORTED,
                        TAG,
                        "only MP3 narration can be preloaded");

    file = fopen(path, "rb");
    if (!file) {
        return ESP_ERR_NOT_FOUND;
    }
    result = decode_mp3_to_memory(file, 0, false, &decoded_pcm, &decoded_samples);
    fclose(file);
    if (result != ESP_OK) {
        return result;
    }

    if (xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        heap_caps_free(decoded_pcm);
        return ESP_ERR_TIMEOUT;
    }

    Mp3CacheEntry *free_entry = nullptr;
    for (size_t i = 0; i < kMaxMp3CacheEntries; ++i) {
        Mp3CacheEntry &entry = s_state.mp3_cache[i];
        if (entry.uri && strcmp(entry.uri, uri) == 0) {
            xSemaphoreGive(s_state.lock);
            heap_caps_free(decoded_pcm);
            return ESP_OK;
        }
        if (!free_entry && !entry.uri) {
            free_entry = &entry;
        }
    }

    if (!free_entry) {
        xSemaphoreGive(s_state.lock);
        heap_caps_free(decoded_pcm);
        return ESP_ERR_NO_MEM;
    }

    free_entry->uri = strdup(uri);
    if (!free_entry->uri) {
        xSemaphoreGive(s_state.lock);
        heap_caps_free(decoded_pcm);
        return ESP_ERR_NO_MEM;
    }
    free_entry->pcm = decoded_pcm;
    free_entry->sample_count = decoded_samples;
    xSemaphoreGive(s_state.lock);

    ESP_LOGI(TAG,
             "preloaded MP3 uri=%s duration_ms=%u pcm_kib=%u",
             uri,
             (unsigned)((decoded_samples * 1000U) / kRequiredSampleRate),
             (unsigned)((decoded_samples * sizeof(int16_t)) / 1024U));
    return ESP_OK;
}

extern "C" void audio_session_register_callback(audio_session_event_cb_t cb, void *user_ctx)
{
    if (!s_state.lock || xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    s_state.callback = cb;
    s_state.callback_ctx = user_ctx;
    xSemaphoreGive(s_state.lock);
}

extern "C" esp_err_t audio_session_start_uri(const char *uri, uint32_t *out_session_id)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    ESP_RETURN_ON_FALSE(uri && uri[0] != '\0', ESP_ERR_INVALID_ARG, TAG, "audio URI is empty");

    (void)audio_session_stop();

    uint32_t session_id = 0;
    uint32_t generation = 0;
    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    session_id = s_state.next_session_id++;
    if (s_state.next_session_id == 0) {
        s_state.next_session_id = 1;
    }
    generation = s_state.generation + 1;
    s_state.generation = generation;
    xSemaphoreGive(s_state.lock);

    const esp_err_t result = enqueue_primary(uri, session_id, generation);
    if (result == ESP_OK && out_session_id) {
        *out_session_id = session_id;
    }
    return result;
}

extern "C" esp_err_t audio_session_play_sfx_uri(const char *uri, int volume_percent)
{
    return audio_session_play_sfx_uri_with_fade_out(uri, volume_percent, 0, 0);
}

extern "C" esp_err_t audio_session_play_sfx_uri_with_fade_out(const char *uri,
                                                              int volume_percent,
                                                              uint32_t fade_out_delay_ms,
                                                              uint32_t fade_out_ms)
{
    char path[256] = {};
    const char *extension = nullptr;
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    ESP_RETURN_ON_ERROR(resolve_audio_uri(uri, path, sizeof(path)), TAG, "SFX URI resolve failed");
    extension = strrchr(path, '.');
    if (extension && strcasecmp(extension, ".pcm") == 0) {
        return s_state.player.playSfxPcmFile(path,
                                            volume_percent,
                                            fade_out_delay_ms,
                                            fade_out_ms) ? ESP_OK : ESP_FAIL;
    }
    if (extension && strcasecmp(extension, ".wav") == 0) {
        return s_state.player.playSfxWavFile(path,
                                            volume_percent,
                                            fade_out_delay_ms,
                                            fade_out_ms) ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

extern "C" esp_err_t audio_session_fade_out_sfx(uint32_t fade_out_ms)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    return s_state.player.fadeOutSfx(fade_out_ms) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

extern "C" esp_err_t audio_session_fade_out_primary(uint32_t fade_out_ms)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    return s_state.player.fadeOutPrimary(fade_out_ms) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

extern "C" esp_err_t audio_session_stop(void)
{
    if (!s_state.initialized) {
        return ESP_OK;
    }

    uint32_t session_id = 0;
    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    session_id = s_state.active_session_id;
    s_state.generation = s_state.generation + 1;
    s_state.active_session_id = 0;
    s_state.active = false;
    s_state.paused = false;
    s_state.last_error = ESP_OK;
    xSemaphoreGive(s_state.lock);

    (void)s_state.player.stop();
    if (s_state.playback_done) {
        xSemaphoreGive(s_state.playback_done);
    }
    if (session_id != 0) {
        emit_event(AUDIO_SESSION_EVENT_STOPPED, session_id, ESP_OK);
    }
    return ESP_OK;
}

extern "C" esp_err_t audio_session_pause(void)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    if (!s_state.player.pause()) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t session_id = 0;
    xSemaphoreTake(s_state.lock, portMAX_DELAY);
    session_id = s_state.active_session_id;
    s_state.paused = true;
    xSemaphoreGive(s_state.lock);
    emit_event(AUDIO_SESSION_EVENT_PAUSED, session_id, ESP_OK);
    return ESP_OK;
}

extern "C" esp_err_t audio_session_resume(void)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    return s_state.player.resume() ? ESP_OK : ESP_ERR_INVALID_STATE;
}

extern "C" esp_err_t audio_session_set_volume(int volume, int *out_volume_set)
{
    int applied = 0;
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "audio session not initialized");
    const bool ok = s_state.player.setVolume(volume, &applied);
    if (out_volume_set) {
        *out_volume_set = applied;
    }
    return ok ? ESP_OK : ESP_FAIL;
}

extern "C" int audio_session_get_volume(void)
{
    return s_state.initialized ? s_state.player.getVolume() : 30;
}

extern "C" bool audio_session_is_muted(void)
{
    return audio_session_get_volume() <= 0;
}

extern "C" void audio_session_get_metrics(audio_session_metrics_t *out_metrics)
{
    if (!out_metrics) {
        return;
    }
    memset(out_metrics, 0, sizeof(*out_metrics));
    if (!s_state.lock || xSemaphoreTake(s_state.lock, portMAX_DELAY) != pdTRUE) {
        return;
    }
    out_metrics->active_session_id = s_state.active_session_id;
    out_metrics->queue_depth = s_state.command_queue ? uxQueueMessagesWaiting(s_state.command_queue) : 0;
    out_metrics->active = s_state.active;
    out_metrics->paused = s_state.paused;
    out_metrics->last_error = s_state.last_error;
    xSemaphoreGive(s_state.lock);
}
