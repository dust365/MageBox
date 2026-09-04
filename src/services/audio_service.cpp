#include "audio_service.h"

#include "board_config.h"
#include "drivers/es8311/es8311.h"
#include "services/volume_service.h"

#include <Arduino.h>
#include <Audio.h>
#include <SD_MMC.h>
#include <cstring>

namespace {

constexpr int mclk_multiple = 256;
constexpr int default_sample_rate = 44100;
constexpr int codec_volume = 75;

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

void pa_enable(bool on)
{
    digitalWrite(PIN_PA_CTRL, on ? HIGH : LOW);
}

void apply_volume(uint8_t percent)
{
    s_audio.setVolume(percent);
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
