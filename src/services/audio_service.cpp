#include "audio_service.h"

#include "board_config.h"
#include "drivers/es8311/es8311.h"
#include "services/volume_service.h"

#include <Arduino.h>
#include <Audio.h>
#include <SD_MMC.h>
#include <cstring>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

template <typename Tag, typename Tag::type Member>
struct PrivateMemberAccess {
    friend typename Tag::type get_private_member(Tag) { return Member; }
};

struct AudioI2sHandle {
    using type = i2s_chan_handle_t Audio::*;
    friend type get_private_member(AudioI2sHandle);
};

template struct PrivateMemberAccess<AudioI2sHandle, &Audio::m_i2s_tx_handle>;

namespace {

constexpr int mclk_multiple = 256;
constexpr int default_sample_rate = 44100;
constexpr int codec_volume = 75;
constexpr size_t pcm_ring_size = 64 * 1024;
constexpr size_t pcm_write_size = 2048;
constexpr uint32_t pcm_write_timeout_ms = 20;
constexpr uint32_t pcm_task_stack = 4096;

Audio s_audio;
es8311_handle_t s_es = nullptr;
bool s_ready = false;
bool s_paused = false;
bool s_eof = false;
bool s_ignore_eof = false;
uint32_t s_codec_rate = 0;
AudioPlayState s_state = AudioPlayState::idle;
char s_title[64] = {};
char s_artist[64] = {};
char s_error[48] = {};
char s_path[80] = {};
uint8_t *s_pcm_ring = nullptr;
size_t s_pcm_read = 0;
size_t s_pcm_write = 0;
size_t s_pcm_filled = 0;
uint32_t s_pcm_rate = 0;
uint8_t s_pcm_channels = 0;
bool s_pcm_active = false;
bool s_pcm_paused = false;
bool s_pcm_started = false;
uint64_t s_pcm_frames_queued = 0;
uint32_t s_pcm_clock_started_us = 0;
uint64_t s_pcm_elapsed_us = 0;
TaskHandle_t s_pcm_task = nullptr;
volatile bool s_pcm_task_exit = false;
volatile bool s_pcm_writing = false;
volatile bool s_pcm_task_running = false;
portMUX_TYPE s_pcm_mux = portMUX_INITIALIZER_UNLOCKED;

i2s_chan_handle_t music_i2s()
{
    return s_audio.*get_private_member(AudioI2sHandle{});
}

bool configure_music_i2s(uint32_t sample_rate)
{
    i2s_chan_handle_t tx = music_i2s();
    if (!tx) return false;
    i2s_channel_disable(tx);
    i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    clock.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    if (i2s_channel_reconfig_std_clock(tx, &clock) != ESP_OK ||
        i2s_channel_reconfig_std_slot(tx, &slot) != ESP_OK ||
        i2s_channel_enable(tx) != ESP_OK) return false;
    return true;
}

void pa_enable(bool on)
{
    digitalWrite(PIN_PA_CTRL, on ? HIGH : LOW);
}

void apply_volume(uint8_t percent)
{
    if (s_pcm_active) {
        if (s_es) es8311_voice_volume_set(s_es, percent, nullptr);
    } else {
        es8311_voice_volume_set(s_es, codec_volume, nullptr);
        s_audio.setVolume(percent);
    }
}

void copy_id3_field(char *dest, size_t dest_n, const char *text)
{
    while (*text == ' ') text++;
    strncpy(dest, text, dest_n - 1);
    dest[dest_n - 1] = 0;
    size_t n = strlen(dest);
    while (n > 0 && (dest[n - 1] == '\r' || dest[n - 1] == '\n' || dest[n - 1] == ' ')) {
        dest[--n] = 0;
    }
}

bool starts_with_ci(const char *text, const char *prefix)
{
    while (*prefix) {
        char a = *text++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

bool codec_apply_rate(uint32_t sample_rate)
{
    if (!s_es || sample_rate < 8000 || sample_rate > 96000) return false;
    if (sample_rate == s_codec_rate) return true;
    const int mclk = (int)sample_rate * mclk_multiple;
    if (es8311_sample_frequency_config(s_es, mclk, (int)sample_rate) != ESP_OK) {
        Serial.printf("ES8311 rate %u Hz failed\n", (unsigned)sample_rate);
        return false;
    }
    s_codec_rate = sample_rate;
    Serial.printf("ES8311 rate %u Hz\n", (unsigned)sample_rate);
    return true;
}

bool codec_init()
{
    s_es = es8311_create(I2C_NUM_0, ES8311_ADDRRES_0);
    if (!s_es) return false;

    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = default_sample_rate * mclk_multiple,
        .sample_frequency = default_sample_rate,
    };
    if (es8311_init(s_es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("ES8311 init failed");
        return false;
    }
    es8311_sample_frequency_config(s_es, default_sample_rate * mclk_multiple, default_sample_rate);
    es8311_voice_volume_set(s_es, codec_volume, nullptr);
    es8311_microphone_config(s_es, false);
    s_codec_rate = default_sample_rate;
    Serial.println("ES8311 ready");
    return true;
}

void stop_pcm_task()
{
    if (!s_pcm_task_running) return;
    s_pcm_task_exit = true;
    while (s_pcm_task_running) vTaskDelay(1);
}

void release_pcm()
{
    pa_enable(false);
    portENTER_CRITICAL(&s_pcm_mux);
    s_pcm_active = false;
    portEXIT_CRITICAL(&s_pcm_mux);
    stop_pcm_task();
    configure_music_i2s(48000);
    codec_apply_rate(48000);
    if (s_pcm_ring) {
        heap_caps_free(s_pcm_ring);
        s_pcm_ring = nullptr;
    }
    s_pcm_read = 0;
    s_pcm_write = 0;
    s_pcm_filled = 0;
    s_pcm_rate = 0;
    s_pcm_channels = 0;
    s_pcm_paused = false;
    s_pcm_started = false;
    s_pcm_frames_queued = 0;
    s_pcm_clock_started_us = 0;
    s_pcm_elapsed_us = 0;
    s_pcm_task_exit = false;
    s_pcm_writing = false;
    s_pcm_task_running = false;
    s_audio.setPinout(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_MCLK);
    apply_volume(volume_service_get());
}

void pcm_ring_push(const uint8_t *data, size_t size)
{
    while (size) {
        const size_t contiguous = (s_pcm_write + size <= pcm_ring_size) ?
            size : pcm_ring_size - s_pcm_write;
        memcpy(s_pcm_ring + s_pcm_write, data, contiguous);
        portENTER_CRITICAL(&s_pcm_mux);
        s_pcm_write = (s_pcm_write + contiguous) % pcm_ring_size;
        s_pcm_filled += contiguous;
        portEXIT_CRITICAL(&s_pcm_mux);
        data += contiguous;
        size -= contiguous;
    }
}

void pcm_output_task(void *)
{
    while (!s_pcm_task_exit) {
        size_t read_pos = 0;
        size_t available = 0;
        portENTER_CRITICAL(&s_pcm_mux);
        if (s_pcm_active && !s_pcm_paused && music_i2s() && s_pcm_ring && s_pcm_filled) {
            read_pos = s_pcm_read;
            available = s_pcm_read + s_pcm_filled <= pcm_ring_size ?
                s_pcm_filled : pcm_ring_size - s_pcm_read;
            if (available > pcm_write_size) available = pcm_write_size;
            s_pcm_writing = true;
        }
        portEXIT_CRITICAL(&s_pcm_mux);

        if (!available) {
            vTaskDelay(1);
            continue;
        }

        size_t written = 0;
        const esp_err_t err = i2s_channel_write(music_i2s(), s_pcm_ring + read_pos,
                                                available, &written,
                                                pcm_write_timeout_ms);
        portENTER_CRITICAL(&s_pcm_mux);
        if ((err == ESP_OK || err == ESP_ERR_TIMEOUT) && written <= s_pcm_filled) {
            s_pcm_read = (s_pcm_read + written) % pcm_ring_size;
            s_pcm_filled -= written;
            s_pcm_frames_queued += written / (sizeof(int16_t) * 2);
        }
        s_pcm_writing = false;
        portEXIT_CRITICAL(&s_pcm_mux);
    }
    s_pcm_writing = false;
    s_pcm_task_running = false;
    s_pcm_task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void audio_info(const char *info)
{
    if (info) Serial.printf("Audio: %s\n", info);
}

void audio_id3data(const char *info)
{
    if (!info) return;
    if (starts_with_ci(info, "Title: ")) copy_id3_field(s_title, sizeof(s_title), info + 7);
    else if (starts_with_ci(info, "Artist: ")) copy_id3_field(s_artist, sizeof(s_artist), info + 8);
}

void audio_eof_mp3(const char *)
{
    if (s_ignore_eof || s_paused) return;
    s_paused = false;
    s_state = AudioPlayState::idle;
    pa_enable(false);
    s_eof = true;
}

bool audio_svc_begin()
{
    if (s_ready) return true;
    pinMode(PIN_PA_CTRL, OUTPUT);
    pa_enable(false);
    if (!codec_init()) {
        strncpy(s_error, "Audio codec init failed", sizeof(s_error) - 1);
        s_state = AudioPlayState::error;
        Serial.println("Audio service: ES8311 init failed");
        return false;
    }
    if (!s_audio.setPinout(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT, PIN_I2S_MCLK)) {
        strncpy(s_error, "I2S init failed", sizeof(s_error) - 1);
        s_state = AudioPlayState::error;
        Serial.println("Audio service: I2S pinout failed");
        return false;
    }
    s_audio.setVolumeSteps(100);
    apply_volume(volume_service_get());
    volume_service_set_output_callback(apply_volume);
    s_ready = true;
    Serial.println("Audio service ready");
    return true;
}

void audio_svc_poll()
{
    if (!s_ready) return;
    if (s_pcm_active) {
        return;
    }
    s_audio.loop();
    if (s_state == AudioPlayState::loading && s_audio.isRunning()) {
        s_state = s_paused ? AudioPlayState::paused : AudioPlayState::playing;
        codec_apply_rate(s_audio.getSampleRate());
        pa_enable(true);
    }
}

void audio_svc_stop()
{
    if (!s_ready) return;
    if (s_pcm_active) {
        release_pcm();
        s_state = AudioPlayState::idle;
        return;
    }
    s_ignore_eof = true;
    s_audio.stopSong();
    s_ignore_eof = false;
    s_paused = false;
    s_state = AudioPlayState::idle;
    pa_enable(false);
}

bool audio_svc_play(const char *path)
{
    if (!s_ready || !path || !path[0]) return false;
    if (s_pcm_active) release_pcm();
    s_ignore_eof = true;
    s_audio.stopSong();
    s_ignore_eof = false;
    s_eof = false;
    s_paused = false;
    s_title[0] = 0;
    s_artist[0] = 0;
    s_error[0] = 0;
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_path[sizeof(s_path) - 1] = 0;
    s_state = AudioPlayState::loading;
    pa_enable(true);
    apply_volume(volume_service_get());
    if (!s_audio.connecttoFS(SD_MMC, path)) {
        s_state = AudioPlayState::error;
        strncpy(s_error, "Can't play file", sizeof(s_error) - 1);
        pa_enable(false);
        Serial.printf("Audio play failed: %s\n", path);
        return false;
    }
    Serial.printf("Audio play: %s\n", path);
    return true;
}

void audio_svc_toggle()
{
    if (!s_ready) return;
    if (s_state != AudioPlayState::playing && s_state != AudioPlayState::paused) {
        return;
    }
    const bool pausing = s_state == AudioPlayState::playing;
    if (!s_audio.pauseResume()) {
        Serial.println("Audio pause/resume failed");
        return;
    }
    s_paused = pausing;
    s_state = s_paused ? AudioPlayState::paused : AudioPlayState::playing;
    pa_enable(!s_paused);
    Serial.println(s_paused ? "Audio paused" : "Audio resumed");
}

bool audio_svc_seek(uint32_t sec)
{
    if (!s_ready) return false;
    if (s_state != AudioPlayState::playing && s_state != AudioPlayState::paused) return false;
    return s_audio.setAudioPlayPosition((uint16_t)sec);
}

AudioStatus audio_svc_status()
{
    AudioStatus status;
    status.state = s_state;
    status.position_sec = s_audio.getAudioCurrentTime();
    status.duration_sec = s_audio.getAudioFileDuration();
    status.sample_rate = s_audio.getSampleRate();
    status.channels = s_audio.getChannels();
    uint16_t vu = (s_state == AudioPlayState::playing) ? s_audio.getVUlevel() : 0;
    uint8_t left = (uint8_t)(vu >> 8);
    uint8_t right = (uint8_t)(vu & 0xFF);
    status.vu_left = left > 127 ? 100 : (uint8_t)((uint16_t)left * 100 / 127);
    status.vu_right = right > 127 ? 100 : (uint8_t)((uint16_t)right * 100 / 127);
    strncpy(status.title, s_title, sizeof(status.title) - 1);
    strncpy(status.artist, s_artist, sizeof(status.artist) - 1);
    status.error = s_error[0] ? s_error : nullptr;
    return status;
}

bool audio_svc_take_eof()
{
    if (!s_eof) return false;
    s_eof = false;
    return true;
}

bool audio_svc_pcm_begin(uint32_t sample_rate, uint8_t bits, uint8_t channels)
{
    if (!s_ready || bits != 16 || (channels != 1 && channels != 2) ||
        sample_rate < 8000 || sample_rate > 96000) return false;
    audio_svc_stop();
    s_pcm_ring = (uint8_t *)heap_caps_malloc(pcm_ring_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pcm_ring) return false;

    if (!configure_music_i2s(sample_rate) || !codec_apply_rate(sample_rate)) {
        release_pcm();
        return false;
    }

    s_pcm_rate = sample_rate;
    s_pcm_channels = channels;
    s_pcm_active = true;
    s_pcm_paused = true;
    s_pcm_started = false;
    s_pcm_frames_queued = 0;
    s_pcm_clock_started_us = 0;
    s_pcm_elapsed_us = 0;
    s_state = AudioPlayState::loading;
    apply_volume(volume_service_get());
    s_pcm_task_exit = false;
    s_pcm_task_running = true;
    if (xTaskCreatePinnedToCore(pcm_output_task, "video_pcm", pcm_task_stack,
                                nullptr, 3, &s_pcm_task, 0) != pdPASS) {
        s_pcm_task_running = false;
        release_pcm();
        return false;
    }
    i2s_channel_disable(music_i2s());
    Serial.printf("PCM audio: %u Hz %u-bit %u ch\n", (unsigned)sample_rate, bits, channels);
    return true;
}

bool audio_svc_pcm_can_accept(size_t input_bytes)
{
    if (!s_pcm_active || !s_pcm_ring || !input_bytes) return false;
    const size_t output_bytes = s_pcm_channels == 1 ? input_bytes * 2 : input_bytes;
    portENTER_CRITICAL(&s_pcm_mux);
    const bool accepted = output_bytes <= pcm_ring_size - s_pcm_filled;
    portEXIT_CRITICAL(&s_pcm_mux);
    return accepted;
}

bool audio_svc_pcm_enqueue(const uint8_t *data, size_t input_bytes)
{
    if (!data || !audio_svc_pcm_can_accept(input_bytes) || (input_bytes & 1U)) return false;
    if (s_pcm_channels == 2) {
        pcm_ring_push(data, input_bytes);
        return true;
    }
    for (size_t i = 0; i < input_bytes; i += sizeof(int16_t)) {
        uint8_t stereo[4] = {data[i], data[i + 1], data[i], data[i + 1]};
        pcm_ring_push(stereo, sizeof(stereo));
    }
    return true;
}

bool audio_svc_pcm_start()
{
    if (!s_pcm_active || s_pcm_started || !s_pcm_filled) return false;
    if (i2s_channel_enable(music_i2s()) != ESP_OK) return false;
    portENTER_CRITICAL(&s_pcm_mux);
    s_pcm_started = true;
    s_pcm_paused = false;
    s_pcm_clock_started_us = micros();
    portEXIT_CRITICAL(&s_pcm_mux);
    pa_enable(true);
    s_state = AudioPlayState::playing;
    return true;
}

void audio_svc_pcm_pause(bool paused)
{
    if (!s_pcm_active || !s_pcm_started || s_pcm_paused == paused) return;
    if (paused) {
        portENTER_CRITICAL(&s_pcm_mux);
        s_pcm_elapsed_us += (uint32_t)(micros() - s_pcm_clock_started_us);
        s_pcm_paused = true;
        portEXIT_CRITICAL(&s_pcm_mux);
        pa_enable(false);
        while (s_pcm_writing) vTaskDelay(1);
        i2s_channel_disable(music_i2s());
        s_state = AudioPlayState::paused;
    } else {
        i2s_channel_enable(music_i2s());
        portENTER_CRITICAL(&s_pcm_mux);
        s_pcm_clock_started_us = micros();
        s_pcm_paused = false;
        portEXIT_CRITICAL(&s_pcm_mux);
        pa_enable(true);
        s_state = AudioPlayState::playing;
    }
}

void audio_svc_pcm_end()
{
    if (!s_pcm_active) return;
    release_pcm();
    s_state = AudioPlayState::idle;
}

uint32_t audio_svc_pcm_position_ms()
{
    if (!s_pcm_active || !s_pcm_rate || !s_pcm_started) return 0;
    portENTER_CRITICAL(&s_pcm_mux);
    uint64_t elapsed_us = s_pcm_elapsed_us;
    if (!s_pcm_paused) elapsed_us += (uint32_t)(micros() - s_pcm_clock_started_us);
    const uint64_t queued_ms = s_pcm_frames_queued * 1000ULL / s_pcm_rate;
    portEXIT_CRITICAL(&s_pcm_mux);
    const uint64_t elapsed_ms = elapsed_us / 1000ULL;
    return (uint32_t)(elapsed_ms < queued_ms ? elapsed_ms : queued_ms);
}

uint32_t audio_svc_pcm_buffered_ms()
{
    if (!s_pcm_active || !s_pcm_rate) return 0;
    portENTER_CRITICAL(&s_pcm_mux);
    const uint64_t supplied_frames = s_pcm_frames_queued + s_pcm_filled / 4U;
    portEXIT_CRITICAL(&s_pcm_mux);
    const uint64_t supplied_ms = supplied_frames * 1000ULL / s_pcm_rate;
    const uint32_t position_ms = audio_svc_pcm_position_ms();
    return supplied_ms > position_ms ? (uint32_t)(supplied_ms - position_ms) : 0;
}
