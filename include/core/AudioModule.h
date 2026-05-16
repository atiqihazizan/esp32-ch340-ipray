#ifndef AUDIO_MODULE_H
#define AUDIO_MODULE_H

#include "Audio.h"
#include "config.h"
#include <SPIFFS.h>
#include <WiFi.h>

extern Audio audio;
extern String audioStatus;

// Kod bahasa TTS masa jalan (boleh diubah dari /config/audio.json)
char activeTtsLang[12] = {0};

#define RAM_BUF_SIZE (8 * 1024)
#define PSRAM_BUF_SIZE (psramFound() ? (32 * 1024) : 0)

#define TTS_QUEUE_DEPTH 4
#define TTS_TEXT_LEN 128

static QueueHandle_t ttsQueue = nullptr;

void enqueueSpeech(const char *text) {
  if (!ttsQueue)
    return;
  char buf[TTS_TEXT_LEN];
  strncpy(buf, text, TTS_TEXT_LEN - 1);
  buf[TTS_TEXT_LEN - 1] = '\0';
  xQueueSend(ttsQueue, buf, 0);
}

// ================================================================
// Hentikan audio yang sedang main + kosongkan queue.
// Guna sebelum beep KEUTAMAAN (cth. waktu masuk solat)
// supaya beep tidak tertunggu warning TTS yang masih main.
// ================================================================
void stopAndFlushAudio() {
  audio.stopSong(); // hentikan stream/fail semasa
  if (ttsQueue)
    xQueueReset(ttsQueue); // buang yang masih dalam queue
  audioStatus = "IDLE";
}

static inline void copyTtsLangBuf(const char *src) {
  if (!src) {
    activeTtsLang[0] = '\0';
    return;
  }
  strncpy(activeTtsLang, src, sizeof(activeTtsLang) - 1);
  activeTtsLang[sizeof(activeTtsLang) - 1] = '\0';
}

// ================================================================
// TASK CORE 0 — route ikut prefix
// ================================================================
void AudioLoopTask(void *pvParameters) {
  char ttsText[TTS_TEXT_LEN];
  for (;;) {
    // Hanya ambil item baru dari queue bila audio TIDAK sedang main
    if (!audio.isRunning()) {
      if (xQueueReceive(ttsQueue, ttsText, 0) == pdTRUE) {

        // ── ROUTE: '/' di permulaan = fail SPIFFS ──
        if (ttsText[0] == '/') {
          // Serial.printf("Audio: play file %s\n", ttsText);
          audio.connecttoFS(SPIFFS, ttsText);
        }
        // ── Selain itu = TTS (perlu WiFi) ──
        else {
          if (WiFi.status() == WL_CONNECTED) {
            audio.connecttospeech(ttsText, activeTtsLang);
          } else {
            Serial.printf("Audio: skip TTS (no WiFi): %s\n", ttsText);
          }
        }
      }
    }
    audio.loop();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void applyAudioRuntimeConfig(int volume, const char *lang) {
  if (volume < 0)
    volume = 0;
  if (volume > MAX_VOL)
    volume = MAX_VOL;
  audio.setVolume(volume);
  if (lang && lang[0] != '\0')
    copyTtsLangBuf(lang);
}

void initAudio() {
  copyTtsLangBuf(TTS_LANG);

  ttsQueue = xQueueCreate(TTS_QUEUE_DEPTH, TTS_TEXT_LEN);
  if (!ttsQueue)
    Serial.println(F("Audio: GAGAL cipta ttsQueue!"));

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DIN);

  // ── VOLUME setup ──
  // setTone(GAIN_LP, GAIN_BP, GAIN_HP) — boost bass/mid/treble (-40..+6 dB)
  //   ↑ mid (BP) buat suara TTS lebih jelas & terasa kuat
  audio.setTone(0, 4, 0); // sedikit boost mid

  audio.setVolume(MAX_VOL);

  audio.setBufsize(RAM_BUF_SIZE, PSRAM_BUF_SIZE);

  if (psramFound()) {
    // Serial.printf("Audio: PSRAM OK — buf RAM=%d PSRAM=%d, vol=%d\n",
    // RAM_BUF_SIZE, (int)PSRAM_BUF_SIZE, MAX_VOL);
  } else {
    Serial.printf("Audio: Tiada PSRAM — buf RAM=%d, vol=%d\n", RAM_BUF_SIZE,
                  MAX_VOL);
  }
}

void audio_info(const char *info) {
  // Serial.print(F("Audio: "));
  // Serial.println(info);

  if (strstr(info, "mp3") || strstr(info, "speech"))
    audioStatus = "TALK";
  else if (strstr(info, "WAV") || strstr(info, "wav"))
    audioStatus = "BEEP";
  else if (strstr(info, "end") || strstr(info, "stop"))
    audioStatus = "IDLE";
}

#endif