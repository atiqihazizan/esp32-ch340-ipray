#ifndef ANNOUNCE_MODULE_H
#define ANNOUNCE_MODULE_H

#include "Audio.h"
#include "core/AudioModule.h" // enqueueSpeech(), stopAndFlushAudio()
#include "logic/BeepModule.h" // beepPrayer(), beepDouble(), beepWarning()
#include "data/ConfigModule.h"
#include "data/PrayerData.h"
#include "RTClib.h"
#include <cstring>
#include <WiFi.h>

extern Audio audio;

// ================================================================
// STRUCT: Entri jadual khas (runtime — diisi dari announce.json)
// ================================================================
struct AnnounceEntry {
  int hour;
  int minute;
  char text[CONFIG_ANNOUNCE_TEXT_LEN];
  int warnBefore; // saat sebelum waktu masuk (0 = tiada amaran)
};

static AnnounceEntry customScheduleRt[CONFIG_CUSTOM_SLOT_MAX];
static int customScheduleRtCount = 0;

// ================================================================
// TOGGLE
// ================================================================
bool announcePrayer       = true;
bool announceCustom       = false;
bool announceEveryMinute  = false;
bool announceEveryQuarter = true;

// Tempoh (minit) paparan kekal pada waktu semasa sebelum tunjuk solat seterusnya.
// Digunakan oleh DisplayModule.h — kemaskini melalui applyAnnounceRuntimeConfig.
int announceNextPrayerPeriodMin = 5;

inline void applyAnnounceRuntimeConfig(const ConfigAnnounce &cfg) {
  announcePrayer = cfg.prayer;
  announceCustom = cfg.custom;
  announceEveryMinute = cfg.everyMinute;
  announceEveryQuarter = cfg.everyQuarter;
  announceNextPrayerPeriodMin = (cfg.nextPrayerPeriodMin > 0) ? cfg.nextPrayerPeriodMin : 0;

  for (int i = 0; i < PRAYER_COUNT && i < CONFIG_PRAYER_SLOT_COUNT; i++) {
    strncpy(prayers[i].announce, cfg.prayerSlots[i].announce,
            PRAYER_ANNOUNCE_LEN - 1);
    prayers[i].announce[PRAYER_ANNOUNCE_LEN - 1] = '\0';
    int wb = cfg.prayerSlots[i].warnBefore;
    if (wb < 0) wb = 0;
    if (wb > 3600) wb = 3600;
    prayers[i].warnBefore = wb;
    int wa = cfg.prayerSlots[i].warnAfterSec;
    if (wa < 0) wa = 0;
    if (wa > 3600) wa = 3600;
    prayers[i].warnAfterSec = wa;
  }

  customScheduleRtCount = 0;
  int n = cfg.customSlotCount;
  if (n > CONFIG_CUSTOM_SLOT_MAX)
    n = CONFIG_CUSTOM_SLOT_MAX;
  for (int i = 0; i < n; i++) {
    customScheduleRt[i].hour = cfg.customSlots[i].hour;
    customScheduleRt[i].minute = cfg.customSlots[i].minute;
    int wb = cfg.customSlots[i].warnBefore;
    if (wb < 0)
      wb = 0;
    if (wb > 3600)
      wb = 3600;
    customScheduleRt[i].warnBefore = wb;
    strncpy(customScheduleRt[i].text, cfg.customSlots[i].text,
            sizeof(customScheduleRt[i].text) - 1);
    customScheduleRt[i].text[sizeof(customScheduleRt[i].text) - 1] = '\0';
    customScheduleRtCount++;
  }
}

// Sesua masa cetusan bagi pampasan kelewatan muat/Google TTS: jadual solat, jadual khas, dan pengumuman
// minit/suku jam. Laraskan ikut rangkaian peranti anda (jangka ±2 hingga ±6 saat lazim).
static const int announceLeadSecondsPerMin = 1;

// Saat efektif hari sama selepas tolak ke hadapan (julat [0, 86399]).
static inline int ledSecondsToday(int totalSec) {
  int v = totalSec + announceLeadSecondsPerMin;
  while (v >= 86400)
    v -= 86400;
  return v;
}

// ================================================================
// TEMPLATE
// ================================================================
void buildTimeText(char *buf, size_t len, int h, int m) {
  int h12 = (h % 12 == 0) ? 12 : h % 12;
  if (m == 0) snprintf(buf, len, "It's %d o'clock", h12);
  else        snprintf(buf, len, "It's %d %02d", h12, m);
  // if (m == 0) snprintf(buf, len, "%d o'clock", h12);
  // else        snprintf(buf, len, "%d %02d", h12, m);
}

// TTS: masa tunggu dalam ayat pembuka «In …» — ≤59 saat guna second;
// ≥60 saat gabungkan hari, jam, minit, (& sisa saat jika ada) ikut bahasa mudah untuk TTS.
static inline size_t appendLeadInDurPart(char *out, size_t cap, size_t u, bool *comma,
                                         int val, const char *sg, const char *pl) {
  if (val <= 0 || u >= cap)
    return u;
  const char *w = (val == 1) ? sg : pl;
  int n = snprintf(out + u, cap - u, "%s%d %s", (*comma) ? ", " : "", val, w);
  if (n > 0) {
    u += (size_t)n;
    *comma = true;
  }
  return u;
}

static void buildLeadInDuration(char *out, size_t outLen, int totalSec) {
  if (outLen <= 8)
    return;
  if (totalSec <= 0) {
    snprintf(out, outLen, "In a moment");
    return;
  }
  if (totalSec <= 59) {
    snprintf(out, outLen, "In %d second%s", totalSec,
             (totalSec == 1) ? "" : "s");
    return;
  }

  int d = totalSec / 86400;
  int r = totalSec % 86400;
  int h = r / 3600;
  r %= 3600;
  int mi = r / 60;
  int se = r % 60;

  size_t u = snprintf(out, outLen, "In ");
  bool comma = false;
  u = appendLeadInDurPart(out, outLen, u, &comma, d, "day", "days");
  u = appendLeadInDurPart(out, outLen, u, &comma, h, "hour", "hours");
  u = appendLeadInDurPart(out, outLen, u, &comma, mi, "minute", "minutes");
  u = appendLeadInDurPart(out, outLen, u, &comma, se, "second", "seconds");
}

// Pecahan tempoh (saat) untuk ayat «telah berlalu» — ≤59 second; ≥60 pecah days/hours/minutes/seconds
static void buildElapsedDurationParts(char *out, size_t outLen, int totalSec) {
  if (outLen < 8)
    return;
  if (totalSec <= 0) {
    snprintf(out, outLen, "a moment");
    return;
  }
  if (totalSec <= 59) {
    snprintf(out, outLen, "%d second%s", totalSec,
             (totalSec == 1) ? "" : "s");
    return;
  }

  int d = totalSec / 86400;
  int r = totalSec % 86400;
  int hPart = r / 3600;
  r %= 3600;
  int mi = r / 60;
  int se = r % 60;

  size_t u     = 0;
  bool   comma = false;
  u = appendLeadInDurPart(out, outLen, u, &comma, d, "day", "days");
  u = appendLeadInDurPart(out, outLen, u, &comma, hPart, "hour", "hours");
  u = appendLeadInDurPart(out, outLen, u, &comma, mi, "minute", "minutes");
  u = appendLeadInDurPart(out, outLen, u, &comma, se, "second", "seconds");
}

// Bina teks amaran ikut secondsBefore untuk TTS.
void buildWarningText(char *buf, size_t len, const char *text,
                      int tH, int tM, int secondsBefore) {
  char prefix[144];
  buildLeadInDuration(prefix, sizeof(prefix), secondsBefore);

  if (text) {
    snprintf(buf, len, "%s, %s", prefix, text);
  } else {
    int h12 = (tH % 12 == 0) ? 12 : tH % 12;
    snprintf(buf, len, "%s, it will be %d %02d", prefix, h12, tM);
  }
}

// ================================================================
// CORE 1: Proses jadual SOLAT (amaran awal, masuk waktu, warn_after)
// Bendera warnAfter: true selepas TTS masuk waktu; warn_after diurus di sini juga.
// ================================================================
static bool prayerWarnAfterPending = false;
static int  prayerWarnAfterSlot    = -1;

bool processPrayerSchedule(int h, int m, int totalSec,
                            int &lastKey, int &lastWarnKey) {
  const int led  = ledSecondsToday(totalSec);
  const int advH = led / 3600;
  const int advM = (led % 3600) / 60;
  (void)h;
  (void)m;

  // --- warn_after: dalam skop jadual solat, tidak gagal dek played suku/minit ---
  if (announcePrayer && prayerWarnAfterPending &&
      prayerWarnAfterSlot >= 0 && prayerWarnAfterSlot < PRAYER_COUNT) {
    int iw = prayerWarnAfterSlot;
    int wa = prayers[iw].warnAfterSec;
    if (wa <= 0) {
      prayerWarnAfterPending = false;
      prayerWarnAfterSlot    = -1;
    } else {
      int ttH        = prayers[iw].hour;
      int ttM        = prayers[iw].minute;
      int warnTarget = ttH * 3600 + ttM * 60 + wa;
      if (totalSec >= warnTarget && totalSec < warnTarget + 60) {
        char dur[96];
        char line[TTS_TEXT_LEN];
        buildElapsedDurationParts(dur, sizeof(dur), wa);
        snprintf(line, sizeof(line),
                 "%s prayer time has passed, %s since prayer time.",
                 prayers[iw].name, dur);
        beepDouble();
        enqueueSpeech(line);
        prayerWarnAfterPending = false;
        prayerWarnAfterSlot    = -1;
        return true;
      }
      if (totalSec > warnTarget + 60) {
        prayerWarnAfterPending = false;
        prayerWarnAfterSlot    = -1;
      }
    }
  }

  for (int i = 0; i < PRAYER_COUNT; i++) {
    int tH        = prayers[i].hour;
    int tM        = prayers[i].minute;
    int wBefore   = prayers[i].warnBefore;
    int entryKey  = tH * 100 + tM;
    int targetSec = tH * 3600 + tM * 60;

    if (wBefore > 0 && lastWarnKey != entryKey) {
      int warnSec = targetSec - wBefore;
      if (led >= warnSec && led <= warnSec + 2) {
        char buf[100];
        buildWarningText(buf, sizeof(buf), prayers[i].announce, tH, tM, wBefore);
        beepWarning();        // ← 3 bip pantas dulu
        enqueueSpeech(buf);   // ← TTS warning (perlu WiFi)
        lastWarnKey = entryKey;
        return true;
      }
    }

    if (advH == tH && advM == tM && lastKey != entryKey) {
      stopAndFlushAudio();                   // ← henti warning TTS / audio semasa
      beepPrayer();                          // ← beep waktu solat (10 bip)
      enqueueSpeech(prayers[i].announce);    // ← TTS lepas tu
      lastKey = entryKey;
      // Aktifkan menunggu peringatan warn_after — tidak bergantung pada played / suku jam
      if (prayers[i].warnAfterSec > 0) {
        prayerWarnAfterPending = true;
        prayerWarnAfterSlot    = i;
      } else {
        prayerWarnAfterPending = false;
        prayerWarnAfterSlot    = -1;
      }
      return true;
    }
  }
  return false;
}

// ================================================================
// CORE 2: Proses jadual KHAS
// ================================================================
bool processSchedule(AnnounceEntry *schedule, int count,
                     int h, int m, int totalSec,
                     int &lastKey, int &lastWarnKey) {
  const int led  = ledSecondsToday(totalSec);
  const int advH = led / 3600;
  const int advM = (led % 3600) / 60;
  (void)h;
  (void)m;

  for (int i = 0; i < count; i++) {
    int tH        = schedule[i].hour;
    int tM        = schedule[i].minute;
    int wBefore   = schedule[i].warnBefore;
    int entryKey  = tH * 100 + tM;
    int targetSec = tH * 3600 + tM * 60;

    if (advH == tH && advM == tM && lastKey != entryKey) {
      stopAndFlushAudio();                   // ← henti audio semasa
      beepDouble();                          // ← beep jadual khas (2 bip)
      char buf[80];
      if (schedule[i].text) {
        strncpy(buf, schedule[i].text, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
      } else {
        buildTimeText(buf, sizeof(buf), advH, advM);
      }
      enqueueSpeech(buf);                    // ← TTS lepas tu
      lastKey = entryKey;
      return true;
    }
  }
  return false;
}

// ================================================================
// TEMPLATE: Optional, tambah teks "quarter past/half past/quarter to"
// ================================================================
void buildQuarterText(char *buf, size_t len, int h, int m) {
  int h12 = (h % 12 == 0) ? 12 : h % 12;

  if (m == 0) {
    snprintf(buf, len, "It's %d o'clock", h12);
  } else if (m == 15) {
    snprintf(buf, len, "It's quarter past %d", h12);
  } else if (m == 30) {
    snprintf(buf, len, "It's half past %d", h12);
  } else if (m == 45) {
    int hNext = (h + 1) % 12;
    if (hNext == 0) hNext = 12;
    snprintf(buf, len, "It's quarter to %d", hNext);
  } else {
    snprintf(buf, len, "It's %d %02d", h12, m);
  }
}

// ================================================================
// MODUL UTAMA — Panggil dalam loop()
// ================================================================
static int lastKeyPrayer  = -1;
static int lastKeyCustom  = -1;
static int lastKeyPerMin  = -1;
static int lastWarnPrayer = -1;
static int lastWarnCustom = -1;

void runAnnounceModule(DateTime now) {
  int h      = now.hour();
  int m      = now.minute();
  int s      = now.second();
  int totSec = h * 3600 + m * 60 + s;

  bool played = false;

  if (announcePrayer)
    played = processPrayerSchedule(h, m, totSec, lastKeyPrayer, lastWarnPrayer);

  if (!played && announceCustom && customScheduleRtCount > 0)
    played = processSchedule(customScheduleRt, customScheduleRtCount, h, m,
                             totSec, lastKeyCustom, lastWarnCustom);

  int vt = ledSecondsToday(totSec);
  const int advSlot = vt / 60;
  const int advH    = vt / 3600;
  const int advM    = (vt % 3600) / 60;

  if (!played && advSlot != lastKeyPerMin) {
    if ((announceEveryQuarter && (advM % 15 == 0)) || announceEveryMinute) {
      char buf[64];
      buildTimeText(buf, sizeof(buf), advH, advM);
      enqueueSpeech(buf);
      lastKeyPerMin = advSlot;
      played = true;
    }
  }
}

#endif