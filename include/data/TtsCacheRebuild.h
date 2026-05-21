#ifndef TTS_CACHE_REBUILD_H
#define TTS_CACHE_REBUILD_H

// Jana semula cache TTS pada SD — satu fail setiap panggilan pump (throttle).

#include "data/ConfigModule.h"
#include "data/PrayerData.h"
#include "data/TtsCacheCore.h"
#include "logic/AnnounceModule.h"
#include "logic/BeepModule.h"
#include "network/TtsGoogleFetch.h"
#include <ArduinoJson.h>

enum TtsTxPhase : uint8_t {
  TTS_TX_ADHAN = 0,
  TTS_TX_WARN,
  TTS_TX_AFTER,
  TTS_TX_CUSTOM,
  TTS_TX_CLK,
  TTS_TX_BELL,
  TTS_TX_IDLE
};

struct TtsRebuildState {
  bool            active;
  TtsTxPhase      phase;
  int             pi;     // prayer index
  int             ci;     // custom index
  int             ch, cm; // jam utk clk_hh_mm
  int             done;
  int             total;
  char            lang[12];
  char            lastId[24];
  unsigned long   nextMs;
};

static TtsRebuildState g_rb = {};
static JsonDocument   *g_rbDoc = nullptr;

static inline void ttsRebuildComputeTotal(const ConfigAnnounce &an) {
  int n = 0;
  for (int i = 0; i < CONFIG_PRAYER_SLOT_COUNT; i++) {
    n++;
    if (an.prayerSlots[i].warnBefore > 0)
      n++;
    if (an.prayerSlots[i].warnAfterSec > 0)
      n++;
  }
  for (int i = 0; i < an.customSlotCount && i < CONFIG_CUSTOM_SLOT_MAX; i++) {
    if (an.customSlots[i].text[0])
      n++;
  }
  n += 24 * 60;
  n++; // bell
  g_rb.total = n;
}

static inline void ttsCacheRebuildAbort() {
  g_rb.active = false;
  g_rb.phase  = TTS_TX_IDLE;
  if (g_rbDoc) {
    delete g_rbDoc;
    g_rbDoc = nullptr;
  }
}

// Mulakan jana semula penuh (perlukan SD + WiFi)
static inline bool ttsCacheRebuildStart(const ConfigAnnounce &an,
                                        const ConfigAudio &aud) {
  ttsCacheRebuildAbort();
  if (!externalAudioReady()) {
    Serial.println(F("TTS rebuild: tiada storan luaran"));
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("TTS rebuild: tiada WiFi"));
    return false;
  }
  fs::FS *fs = externalAudioFs();
  if (!ttsCacheEnsureDirs(*fs)) {
      Serial.println(F("TTS rebuild: mkdir /ann gagal"));
      return false;
  }

  g_rbDoc = new JsonDocument();
  if (!g_rbDoc) {
    Serial.println(F("TTS rebuild: OOM dokumen"));
    return false;
  }
  if (!ttsManifestLoad(*g_rbDoc)) {
    g_rbDoc->clear();
    (*g_rbDoc)["v"] = 1;
  }
  (*g_rbDoc)["lang"] = aud.ttsLang;
  ttsRebuildComputeTotal(an);
  strncpy(g_rb.lang, aud.ttsLang, sizeof(g_rb.lang) - 1);
  g_rb.lang[sizeof(g_rb.lang) - 1] = '\0';

  g_rb.active = true;
  g_rb.phase  = TTS_TX_ADHAN;
  g_rb.pi     = 0;
  g_rb.ci     = 0;
  g_rb.ch     = 0;
  g_rb.cm     = 0;
  g_rb.done   = 0;
  g_rb.nextMs = 0;
  g_rb.lastId[0] = '\0';
  Serial.printf("TTS rebuild: mula — jumlah=%d\n", g_rb.total);
  return true;
}

static inline bool ttsCacheRebuildActive() { return g_rb.active; }

static inline void ttsCacheRebuildProgress(int *done, int *total,
                                         char *lastId, size_t lastCap) {
  if (done)
    *done = g_rb.done;
  if (total)
    *total = g_rb.total;
  if (lastId && lastCap > 0) {
    strncpy(lastId, g_rb.lastId, lastCap - 1);
    lastId[lastCap - 1] = '\0';
  }
}

// Satu langkah; pulangkan true jika masih aktif / sibuk
static inline bool ttsCacheRebuildPump(const ConfigAnnounce &an,
                                       const ConfigAudio &aud) {
  if (!g_rb.active || !g_rbDoc || !externalAudioReady())
    return false;
  if (millis() < g_rb.nextMs)
    return true;
  if (WiFi.status() != WL_CONNECTED) {
    g_rb.nextMs = millis() + 3000;
    return true;
  }

  fs::FS *fs = externalAudioFs();
  const char *lang = g_rb.lang[0] ? g_rb.lang : aud.ttsLang;

  auto finishOne = [&](const char *id, const char *text) {
    char rel[96];
    ttsCacheRelPathMp3(id, rel, sizeof(rel));
    uint32_t h = ttsCacheHashText(text, lang);
    if (ttsCacheEntryValid(*fs, *g_rbDoc, id, text, lang)) {
      g_rb.done++;
      snprintf(g_rb.lastId, sizeof(g_rb.lastId), "%s (skip)", id);
      g_rb.nextMs = millis() + 80;
      return;
    }
    if (!fetchGoogleTtsToFile(*fs, rel, text, lang)) {
      Serial.printf("TTS rebuild: GAGAL %s\n", id);
      ttsManifestSetEntry(*g_rbDoc, id, h, 0, false);
    } else {
      File vf = fs->open(rel, "r");
      size_t z = vf ? vf.size() : 0;
      if (vf)
        vf.close();
      ttsManifestSetEntry(*g_rbDoc, id, h, z, true);
    }
    g_rb.done++;
    strncpy(g_rb.lastId, id, sizeof(g_rb.lastId) - 1);
    ttsManifestSave(*g_rbDoc);
    g_rb.nextMs = millis() + 400;
  };

  switch (g_rb.phase) {
  case TTS_TX_ADHAN:
    if (g_rb.pi >= CONFIG_PRAYER_SLOT_COUNT) {
      g_rb.phase = TTS_TX_WARN;
      g_rb.pi    = 0;
      return true;
    }
    {
      char id[20];
      snprintf(id, sizeof(id), "p%d_adhan", g_rb.pi);
      finishOne(id, prayers[g_rb.pi].announce);
      g_rb.pi++;
    }
    break;

  case TTS_TX_WARN:
    if (g_rb.pi >= CONFIG_PRAYER_SLOT_COUNT) {
      g_rb.phase = TTS_TX_AFTER;
      g_rb.pi    = 0;
      return true;
    }
    if (prayers[g_rb.pi].warnBefore <= 0) {
      g_rb.pi++;
      return true;
    }
    {
      char id[20], buf[200];
      snprintf(id, sizeof(id), "p%d_warn", g_rb.pi);
      buildWarningText(buf, sizeof(buf), prayers[g_rb.pi].announce,
                       prayers[g_rb.pi].hour, prayers[g_rb.pi].minute,
                       prayers[g_rb.pi].warnBefore);
      finishOne(id, buf);
      g_rb.pi++;
    }
    break;

  case TTS_TX_AFTER:
    if (g_rb.pi >= CONFIG_PRAYER_SLOT_COUNT) {
      g_rb.phase = TTS_TX_CUSTOM;
      g_rb.ci    = 0;
      return true;
    }
    if (prayers[g_rb.pi].warnAfterSec <= 0) {
      g_rb.pi++;
      return true;
    }
    {
      char id[28], dur[96], line[200];
      snprintf(id, sizeof(id), "p%d_after_%d", g_rb.pi,
               prayers[g_rb.pi].warnAfterSec);
      buildElapsedDurationParts(dur, sizeof(dur), prayers[g_rb.pi].warnAfterSec);
      snprintf(line, sizeof(line),
               "%s prayer time has passed, %s since prayer time.",
               prayers[g_rb.pi].name, dur);
      finishOne(id, line);
      g_rb.pi++;
    }
    break;

  case TTS_TX_CUSTOM:
    if (g_rb.ci >= an.customSlotCount ||
        g_rb.ci >= CONFIG_CUSTOM_SLOT_MAX) {
      g_rb.phase = TTS_TX_CLK;
      g_rb.ch    = 0;
      g_rb.cm    = 0;
      return true;
    }
    if (!an.customSlots[g_rb.ci].text[0]) {
      g_rb.ci++;
      return true;
    }
    {
      char id[12];
      snprintf(id, sizeof(id), "c%02d", g_rb.ci);
      finishOne(id, an.customSlots[g_rb.ci].text);
      g_rb.ci++;
    }
    break;

  case TTS_TX_CLK: {
    if (g_rb.ch >= 24) {
      g_rb.phase = TTS_TX_BELL;
      return true;
    }
    char id[20], buf[64];
    snprintf(id, sizeof(id), "clk_%02d_%02d", g_rb.ch, g_rb.cm);
    buildTimeText(buf, sizeof(buf), g_rb.ch, g_rb.cm);
    finishOne(id, buf);
    g_rb.cm++;
    if (g_rb.cm >= 60) {
      g_rb.cm = 0;
      g_rb.ch++;
    }
  } break;

  case TTS_TX_BELL: {
    char bellPath[64];
    ttsCacheRelPathBell(bellPath, sizeof(bellPath));
    strncpy(g_rb.lastId, "bell_hour.wav", sizeof(g_rb.lastId) - 1);
    ensureBellHourWavFs(*fs, bellPath);
    g_rb.done++;
    (*g_rbDoc)["bell_ok"] = true;
    ttsManifestSave(*g_rbDoc);
    g_rb.phase = TTS_TX_IDLE;
    g_rb.active = false;
    delete g_rbDoc;
    g_rbDoc = nullptr;
    Serial.println(F("TTS rebuild: SIAP"));
    return false;
  }

  default:
    return false;
  }

  return g_rb.active;
}

#endif
