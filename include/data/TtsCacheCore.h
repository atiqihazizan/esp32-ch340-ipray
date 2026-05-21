#ifndef TTS_CACHE_CORE_H
#define TTS_CACHE_CORE_H

// Manifest hash + laluan cache TTS pada storan luaran (/ann/*.mp3).
// Manifest disimpan di SPIFFS (kecil).

#include "core/AudioStorageModule.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <SPIFFS.h>
#include <cstring>

#define TTS_MANIFEST_PATH "/config/tts_manifest.json"
#define TTS_ANN_DIR       "/ann"
#define TTS_ANN_SYS_DIR   "/ann/sys"

// ----------------------------------------------------------------
inline uint32_t ttsCacheHashText(const char *text, const char *lang) {
  if (!text)
    text = "";
  if (!lang)
    lang = "";
  uint32_t h = 2166136261u;
  for (const char *p = text; *p; ++p) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  h ^= (uint8_t)'|';
  h *= 16777619u;
  for (const char *p = lang; *p; ++p) {
    h ^= (uint8_t)*p;
    h *= 16777619u;
  }
  return h;
}

// id: contoh p0_adhan, p0_warn, c03, clk_14_30, p0_after_300
inline void ttsCacheRelPathMp3(const char *id, char *out, size_t cap) {
  snprintf(out, cap, "%s/%s.mp3", TTS_ANN_DIR, id);
}

inline void ttsCacheRelPathBell(char *out, size_t cap) {
  snprintf(out, cap, "%s/bell_hour.wav", TTS_ANN_SYS_DIR);
}

inline bool ttsCacheEnsureDirs(fs::FS &fs) {
  if (!fs.exists(TTS_ANN_DIR) && !fs.mkdir(TTS_ANN_DIR))
    return false;
  if (!fs.exists(TTS_ANN_SYS_DIR) && !fs.mkdir(TTS_ANN_SYS_DIR))
    return false;
  return true;
}

inline bool ttsManifestLoad(JsonDocument &doc) {
  File f = SPIFFS.open(TTS_MANIFEST_PATH, "r");
  if (!f)
    return false;
  String raw = f.readString();
  f.close();
  raw.trim();
  if (raw.length() == 0)
    return false;
  return !deserializeJson(doc, raw);
}

inline bool ttsManifestSave(JsonDocument &doc) {
  if (!SPIFFS.begin(false))
    return false;
  SPIFFS.mkdir("/config");
  String out;
  serializeJson(doc, out);
  File f = SPIFFS.open(TTS_MANIFEST_PATH, "w");
  if (!f)
    return false;
  f.print(out);
  f.close();
  return true;
}

// Padam manifest — panggil bila tts_lang ditukar supaya cache dijana semula.
inline bool ttsManifestRemove() {
  if (!SPIFFS.exists(TTS_MANIFEST_PATH))
    return true;
  return SPIFFS.remove(TTS_MANIFEST_PATH);
}

inline uint32_t ttsManifestEntryHash(JsonDocument &doc, const char *id) {
  if (doc["e"][id]["h"].isNull())
    return 0;
  return (uint32_t)doc["e"][id]["h"].as<unsigned long>();
}

inline void ttsManifestSetEntry(JsonDocument &doc, const char *id,
                                uint32_t hash, size_t sz, bool ok) {
  doc["e"][id]["h"]  = hash;
  doc["e"][id]["sz"] = (unsigned)sz;
  doc["e"][id]["ok"]  = ok;
}

inline void ttsManifestSetLang(JsonDocument &doc, const char *lang) {
  doc["lang"] = lang;
}

inline const char *ttsManifestLang(JsonDocument &doc) {
  return doc["lang"] | "";
}

inline bool ttsCacheFileLooksOk(fs::FS &fs, const char *relPath) {
  if (!fs.exists(relPath))
    return false;
  File f = fs.open(relPath, "r");
  if (!f)
    return false;
  size_t z = f.size();
  f.close();
  return z >= 256;
}

inline bool ttsCacheEntryValid(fs::FS &fs, JsonDocument &doc, const char *id,
                               const char *text, const char *lang) {
  uint32_t want = ttsCacheHashText(text, lang);
  uint32_t got  = ttsManifestEntryHash(doc, id);
  if (got != want)
    return false;
  char path[96];
  ttsCacheRelPathMp3(id, path, sizeof(path));
  return ttsCacheFileLooksOk(fs, path);
}

#endif
