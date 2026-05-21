#ifndef BEEP_MODULE_H
#define BEEP_MODULE_H

#include "core/AudioModule.h"
#include "esp_task_wdt.h"
#include <FS.h>
#include <SPIFFS.h>
#include <math.h>

// ================================================================
// TETAPAN BEEP
// ================================================================
// PENTING: Library audioI2S buffer = ~6591 bytes (no PSRAM).
// Fail WAV mesti LEBIH BESAR dari buffer.
//
// ── VOLUME ──
//   BEEP_AMP max = 32767 (16-bit signed peak)
//   30000 ≈ 91% — kuat tapi tak hard-clip
//   32000 ≈ 97% — paling kuat (mungkin sedikit distort)
//
// ── FREKUENSI ──
//   2000–3000 Hz = paling sensitif kepada telinga manusia
//   2600 Hz dah optimal — JANGAN tukar lagi tinggi.
//
#define BEEP_SR 16000  // sample rate (Hz)
#define BEEP_FREQ 2600 // frekuensi (Hz)
#define BEEP_AMP 30000 // amplitud (max 32767)

// ── REGENERATE FLAG ──
//   1 = sentiasa padam & jana semula (development — apply parameter baru)
//   0 = guna fail sedia ada (production — jimat write cycle flash)
#define BEEP_FORCE_REGEN 0

// ================================================================
// HELPER: Tulis WAV header (44 bytes)
// ================================================================
template <typename W>
static void _wavHdr(W &f, uint32_t dataBytes) {
  uint16_t ch = 1, bps = 16, fmt = 1, ba = 2;
  uint32_t sr = BEEP_SR, br = BEEP_SR * 2, s1 = 16;
  uint32_t riff = 36 + dataBytes;

  f.write((uint8_t *)"RIFF", 4);
  f.write((uint8_t *)&riff, 4);
  f.write((uint8_t *)"WAVE", 4);
  f.write((uint8_t *)"fmt ", 4);
  f.write((uint8_t *)&s1, 4);
  f.write((uint8_t *)&fmt, 2);
  f.write((uint8_t *)&ch, 2);
  f.write((uint8_t *)&sr, 4);
  f.write((uint8_t *)&br, 4);
  f.write((uint8_t *)&ba, 2);
  f.write((uint8_t *)&bps, 2);
  f.write((uint8_t *)"data", 4);
  f.write((uint8_t *)&dataBytes, 4);
}

// ================================================================
// HELPER: Tulis sampel sine wave dengan envelope ringkas
// (elak click/pop di permulaan & penghujung beep)
// ================================================================
template <typename W>
static void _sine(W &f, int ms) {
  int n = BEEP_SR * ms / 1000;
  int fade = BEEP_SR * 5 / 1000; // 5ms fade in/out
  if (fade > n / 2)
    fade = n / 2;

  for (int i = 0; i < n; i++) {
    if ((i & 0x3FF) == 0) {
      esp_task_wdt_reset();
      yield();
    }
    float env = 1.0f;
    if (i < fade)
      env = (float)i / fade;
    else if (i >= n - fade)
      env = (float)(n - i) / fade;

    int16_t s =
        (int16_t)(env * BEEP_AMP * sin(2.0 * M_PI * BEEP_FREQ * i / BEEP_SR));
    f.write((uint8_t *)&s, 2);
  }
}

// ================================================================
// HELPER: Tulis senyap (silence)
// ================================================================
template <typename W>
static void _sil(W &f, int ms) {
  int n = BEEP_SR * ms / 1000;
  int16_t z = 0;
  for (int i = 0; i < n; i++) {
    if ((i & 0x3FF) == 0) {
      esp_task_wdt_reset();
      yield();
    }
    f.write((uint8_t *)&z, 2);
  }
}

// ================================================================
// Skip jana jika SPIFFS sudah ada fail
// ================================================================
static bool _checkSkip(const char *path) {
  if (BEEP_FORCE_REGEN) {
    if (SPIFFS.exists(path))
      SPIFFS.remove(path);
    return false;
  }
  return SPIFFS.exists(path);
}

// ================================================================
// JANA WAV — pattern array (untuk kes custom)
// ================================================================
static void _makeWav(const char *path, int *bMs, int *sMs, int count) {
  if (_checkSkip(path))
    return;

  int totalSamples = 0;
  for (int i = 0; i < count; i++)
    totalSamples += (bMs[i] + sMs[i]) * BEEP_SR / 1000;

  int padMs = 250;
  totalSamples += padMs * BEEP_SR / 1000;

  uint32_t dataBytes = totalSamples * 2;

  File f = SPIFFS.open(path, "w");
  if (!f) {
    Serial.printf("Beep: gagal buat %s\n", path);
    return;
  }

  _wavHdr(f, dataBytes);
  for (int i = 0; i < count; i++) {
    if (bMs[i] > 0)
      _sine(f, bMs[i]);
    if (sMs[i] > 0)
      _sil(f, sMs[i]);
  }
  _sil(f, padMs);
  f.close();
  Serial.printf("Beep: %s siap (%d bytes)\n", path, 44 + (int)dataBytes);
}

// ================================================================
// JANA WAV — pattern set berulang
// ================================================================
static void _makeWavSet(const char *path, int beepMs, int shortMs,
                        int longMs, int sets) {
  if (_checkSkip(path))
    return;

  int padMs = 250;
  int oneSetMs = beepMs + shortMs + beepMs;
  int totalMs = oneSetMs * sets + longMs * (sets - 1) + padMs;
  uint32_t dataBytes = (uint32_t)(totalMs * BEEP_SR / 1000) * 2;

  File f = SPIFFS.open(path, "w");
  if (!f) {
    Serial.printf("Beep: gagal buat %s\n", path);
    return;
  }

  _wavHdr(f, dataBytes);
  for (int i = 0; i < sets; i++) {
    _sine(f, beepMs);
    _sil(f, shortMs);
    _sine(f, beepMs);
    if (i < sets - 1)
      _sil(f, longMs);
  }
  _sil(f, padMs);
  f.close();
  Serial.printf("Beep: %s siap (%d bytes)\n", path, 44 + (int)dataBytes);
}

// ================================================================
// HELPER: Padam fail lama yang tidak lagi digunakan (SPIFFS sahaja)
// ================================================================
static void _removeOldWav(const char *path) {
  if (SPIFFS.exists(path)) {
    SPIFFS.remove(path);
    Serial.printf("Beep: %s dipadam (tidak lagi digunakan)\n", path);
  }
}

// ================================================================
// FUNGSI BEEP
// ================================================================
void beepDouble() { enqueueSpeech("/b2.wav"); }
void beepPrayer() { enqueueSpeech("/ba.wav"); }
void beepWarning() { enqueueSpeech("/b3.wav"); }

// Loceng jam penuh — WAV pada storan luaran /ann/sys/bell_hour.wav
// Nada tunggal ~450ms @ 900 Hz (lebih rendah daripada beep amaran)
#define BELL_HOUR_FREQ 900
#define BELL_HOUR_MS   450

template <typename W>
static void _sineMsFreq(W &f, int ms, int freqHz) {
  int n = BEEP_SR * ms / 1000;
  int fade = BEEP_SR * 8 / 1000;
  if (fade > n / 2) fade = n / 2;
  for (int i = 0; i < n; i++) {
    if ((i & 0x3FF) == 0) {
      esp_task_wdt_reset();
      yield();
    }
    float env = 1.0f;
    if (i < fade) env = (float)i / fade;
    else if (i >= n - fade) env = (float)(n - i) / fade;
    int16_t s = (int16_t)(env * BEEP_AMP *
                          sin(2.0 * M_PI * freqHz * i / BEEP_SR));
    f.write((uint8_t *)&s, 2);
  }
}

// Jana bell_hour.wav pada fs (SD) jika belum ada; path relatif contoh /ann/sys/bell_hour.wav
template <typename W>
static void _makeBellWavFs(W &f, int ms, int freqHz) {
  int padMs = 200;
  uint32_t dataBytes =
      (uint32_t)((ms + padMs) * BEEP_SR / 1000) * 2;
  _wavHdr(f, dataBytes);
  _sineMsFreq(f, ms, freqHz);
  int nZ = BEEP_SR * padMs / 1000;
  int16_t z = 0;
  for (int i = 0; i < nZ; i++) {
    if ((i & 0x3FF) == 0) {
      esp_task_wdt_reset();
      yield();
    }
    f.write((uint8_t *)&z, 2);
  }
}

inline void ensureBellHourWavFs(fs::FS &fs, const char *relPath) {
  if (fs.exists(relPath))
    return;
  File f = fs.open(relPath, "w", true);
  if (!f) {
    Serial.printf("Beep: gagal jana %s\n", relPath);
    return;
  }
  _makeBellWavFs(f, BELL_HOUR_MS, BELL_HOUR_FREQ);
  f.close();
  Serial.printf("Beep: %s siap (loceng jam)\n", relPath);
}

// ================================================================
// INIT — WAV dalam SPIFFS sahaja
// ================================================================
void initBeeps() {
  // SPIFFS sudah dimulakan oleh initConfigStorage() dengan maxOpenFiles=24
  // — jangan panggil SPIFFS.begin() semula (akan set semula ke maxOpenFiles=5)
  _removeOldWav("/beep.wav");
  _removeOldWav("/beep1.wav");
  _removeOldWav("/beep2.wav");
  // _removeOldWav("/b3.wav");

  {
    File ch = SPIFFS.open("/ba.wav", "r");
    if (ch && ch.size() > 620000L) {
      ch.close();
      SPIFFS.remove("/ba.wav");
      Serial.println(F("Beep: /ba.wav lama terlalu besar — dipadam, akan jana semula"));
    } else if (ch)
      ch.close();
  }

  {
    int b[] = {70};
    int s[] = {0};
    _makeWav("/b1.wav", b, s, 1);
  }

  _makeWavSet("/b2.wav", 70, 50, 0, 1);
  _makeWavSet("/ba.wav", 60, 50, 1700, 8);
  _makeWavSet("/b3.wav", 60, 50, 1700, 3);

  Serial.println(F("Boot beep..."));
  // beepDouble();
}

#endif
