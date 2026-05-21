#ifndef CONFIG_MODULE_H
#define CONFIG_MODULE_H

#include "config.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <cerrno>

// ================================================================
// CONFIG MODULE — Fasa 2
// JSON dalam SPIFFS /config/; jika fail hilang guna lalai dari config.h /
// nilai sandaran padat di bawah.
// ================================================================

#define CONFIG_PATH_WIFI "/config/wifi.json"
#define CONFIG_PATH_TAKWIM "/config/takwim.json"
#define CONFIG_PATH_AUDIO "/config/audio.json"
#define CONFIG_PATH_ANNOUNCE "/config/announce.json"

// Lalai takwim / hostname (selari CLAUDE Tier 3 & mDNS)
#define CFG_DEFAULT_HOSTNAME "my-clock"
#define CFG_DEFAULT_TAKWIM_ZONE "PNG01"
#define CFG_DEFAULT_TAKWIM_URL                                                                     \
  "https://www.e-solat.gov.my/index.php?r=esolatApi/takwimsolat&period=year&zone=%s"

// ================================================================
// STRUCT CONFIG
// ================================================================
struct ConfigWiFi {
  char ssid[33];
  char password[65];
  char hostname[33];
};

struct ConfigTakwim {
  char url[384]; // URL e-solat + parameter boleh panjang — elak truncation
  char zone[16];
};

struct ConfigAudio {
  int volume;
  char ttsLang[12];
};

// Selari susunan prayers[] dalam PrayerData.h (Subuh … Isyak)
#define CONFIG_PRAYER_SLOT_COUNT 6
#define CONFIG_ANNOUNCE_TEXT_LEN 128
#define CONFIG_CUSTOM_SLOT_MAX 16

struct ConfigPrayerSlot {
  char announce[CONFIG_ANNOUNCE_TEXT_LEN];
  int warnBefore;
  int warnAfterSec; // peringatan selepas waktu masuk (saat, 0 = mati, lalai 300)
};

struct ConfigCustomSlot {
  int hour;
  int minute;
  char text[CONFIG_ANNOUNCE_TEXT_LEN];
  int warnBefore;
};

struct ConfigAnnounce {
  bool prayer;
  bool custom;
  bool everyMinute;
  bool everyQuarter;
  int nextPrayerPeriodMin; // tempoh (minit) paparan kekal pada waktu semasa sebelum tunjuk solat seterusnya (lalai 5)
  bool quarterHourBeep;    // beepDouble pada :15 :30 :45 (perlu SD untuk cache solat; beep sendiri di SPIFFS)
  bool hourlyBell;         // loceng WAV pada :00 (fail /ann/sys/bell_hour.wav pada SD)
  ConfigPrayerSlot prayerSlots[CONFIG_PRAYER_SLOT_COUNT];
  ConfigCustomSlot customSlots[CONFIG_CUSTOM_SLOT_MAX];
  int customSlotCount;
};

// ================================================================
// HELPER
// ================================================================
static inline void configSafeCopy(char *dst, size_t n, const char *src) {
  if (!dst || n == 0)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, n - 1);
  dst[n - 1] = '\0';
}

// ── Sapu placeholder kosong masa boot sahaja — guna panjang tekst selepas readString ──
static inline void configPadamJsonKosongBoot(const char *path) {
  File f = SPIFFS.open(path, "r");
  if (!f)
    return;
  String raw = f.readString();
  f.close();
  raw.trim();
  if (raw.length() > 0)
    return;
  SPIFFS.remove(path);
  Serial.printf("Config: boot — `%s` tiada JSON — dipadam (placeholder uploadfs)\n",
                path);
}

// ── SPIFFS/File: size() boleh 0 salah bila baru tulis → baca keseluruhan fail + parse ──
static inline bool configTryLoadJsonDoc(const char *path, const char *labelFail,
                                         JsonDocument &doc) {
  File f = SPIFFS.open(path, "r");
  if (!f)
    return false;
  String raw = f.readString();
  f.close();
  raw.trim();
  if (raw.length() == 0) {
    Serial.printf("Config: `%s` tiada kiriman JSON (guna lalai)\n", path);
    return false;
  }

  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    Serial.printf("Config: %s — %s\n", labelFail, err.c_str());
    return false;
  }
  return true;
}

// Mulakan SPIFFS (panggil awal setup, sebelum baca JSON)
inline bool initConfigStorage() {
  // maxOpenFiles lebih tinggi: elak VFS fopen gagal bila banyak handle
  // (web + audio + config) serentak; lalai 10 terlalu cetek.
  if (!SPIFFS.begin(true, "/spiffs", 24)) {
    Serial.println(F("Config: SPIFFS gagal dimulakan"));
    return false;
  }
  // Pastikan "direktori" /config wujud (VFS / mkdir — sesetengah board perlu ini)
  SPIFFS.mkdir("/config");

  // One-shot cleanup: display.json deprecated (no UI, no route, never read).
  // This block can be removed in a future release after all devices have
  // booted at least once with this firmware.
  if (SPIFFS.exists("/config/display.json")) {
    SPIFFS.remove("/config/display.json");
    Serial.println(F("Config: display.json dipadam (deprecated)"));
  }

  // Padam fail config yang benar‑benar kosong (hasil uploadfs) — sekali boot
  configPadamJsonKosongBoot(CONFIG_PATH_WIFI);
  configPadamJsonKosongBoot(CONFIG_PATH_TAKWIM);
  configPadamJsonKosongBoot(CONFIG_PATH_AUDIO);
  configPadamJsonKosongBoot(CONFIG_PATH_ANNOUNCE);

  return true;
}

// ── Tulis satu fail JSON /config/*.json + log konsol ─────────────────
static inline bool configWriteJsonFile(const char *tajuk, const char *path,
                                       JsonDocument &doc) {
  if (!SPIFFS.begin(false)) {
    Serial.printf("Config: tulis gagal [%s] %s — SPIFFS.begin gagal\n", tajuk,
                  path);
    return false;
  }

  size_t      tbSPI = SPIFFS.totalBytes(), ub = SPIFFS.usedBytes();
  unsigned long bebas = (tbSPI > ub) ? (unsigned long)(tbSPI - ub) : 0UL;
  Serial.printf("Config: simpan [%s] `%s` (bebasSPIFFS=%lu/%lu bait)\n", tajuk,
                path, bebas, (unsigned long)tbSPI);

  SPIFFS.mkdir("/config");

  // Ganti atomik lapangan: beberapa imej gagal truncate `w` jika rantai FAT
  // SPIFFS bermasalah; padam dahulu kemudian buka baharu.
  if (SPIFFS.exists(path) && !SPIFFS.remove(path)) {
    Serial.printf("Config: amaran — gagal padam `%s` sebelum tulis\n", path);
  }

  File f = SPIFFS.open(path, "w");
  if (!f) {
    int e = errno;
    size_t tb = SPIFFS.totalBytes(), ub2 = SPIFFS.usedBytes();
    unsigned long bf = (tb > ub2) ? (unsigned long)(tb - ub2) : 0UL;
    Serial.printf("Config: tulis gagal [%s] buka `%s` (bebas %lu bait, errno=%d)\n",
                  tajuk, path, bf, e);
    return false;
  }

  size_t nbytes = serializeJson(doc, f);
  if (nbytes == 0) {
    f.close();
    Serial.printf(
        "Config: tulis gagal [%s] `%s` — serializeJson 0 bait\n", tajuk, path);
    return false;
  }
  f.flush();
  f.close();
  yield(); // bagi VFS/cache SPIFFS masa sebelum baca sah

  File ver = SPIFFS.open(path, "r");
  if (!ver) {
    Serial.printf(
        "Config: AMARAN sah [%s] — `%s` tak boleh dibuka semula selepas tulis\n",
        tajuk, path);
  } else {
    String lagi = ver.readString();
    ver.close();
    if ((unsigned)lagi.length() < nbytes) {
      Serial.printf(
          "Config: AMARAN sah `%s`: baca %u bait, tulis dakwa %u bait\n",
          path, (unsigned)lagi.length(), (unsigned)nbytes);
    }
  }

  Serial.printf("Config: simpan OK [%s] `%s` (%u bait)\n", tajuk, path,
                (unsigned)nbytes);
  return true;
}

// ── Lalai padat (fallback penuh) ─────────────────────────────
inline ConfigWiFi defaultWiFiConfig() {
  ConfigWiFi w = {};
  configSafeCopy(w.ssid, sizeof(w.ssid), WIFI_SSID);
  configSafeCopy(w.password, sizeof(w.password), WIFI_PASS);
  configSafeCopy(w.hostname, sizeof(w.hostname), CFG_DEFAULT_HOSTNAME);
  return w;
}

inline ConfigTakwim defaultTakwimConfig() {
  ConfigTakwim t = {};
  configSafeCopy(t.url, sizeof(t.url), CFG_DEFAULT_TAKWIM_URL);
  configSafeCopy(t.zone, sizeof(t.zone), CFG_DEFAULT_TAKWIM_ZONE);
  return t;
}

inline ConfigAudio defaultAudioConfig() {
  ConfigAudio a;
  a.volume = MAX_VOL;
  configSafeCopy(a.ttsLang, sizeof(a.ttsLang), TTS_LANG);
  return a;
}

static inline void configAnnouncePrayerDefaults(ConfigPrayerSlot *slots) {
  const char *a[] = {
      "It is now Soobooh prayer time",
      "It is now Shooroock time",
      "It is now Zohor prayer time",
      "It is now Asarr prayer time",
      "It is now Mughrib prayer time",
      "It is now Ishaa prayer time",
  };
  // Selari PrayerData.h: warn_before / warn_after (saat)
  const int w[] = {600, 0, 300, 300, 300, 180};
  const int wa[] = {300, 0, 300, 300, 300, 300};
  for (int i = 0; i < CONFIG_PRAYER_SLOT_COUNT; i++) {
    configSafeCopy(slots[i].announce, sizeof(slots[i].announce), a[i]);
    slots[i].warnBefore = w[i];
    slots[i].warnAfterSec = wa[i];
  }
}

static inline void configAnnounceCustomDefaults(ConfigAnnounce &x) {
  struct Row {
    int h, m, wb;
    const char *t;
  };
  static const Row rows[] = {
      {6, 30, 0, "Masa sarapan pagi"},
      {12, 30, 0, "Masa makan tengah hari"},
      {14, 21, 0, "Sudah lewat petang, tidak ada masa untuk sholat"},
      {22, 0, 0, "Sudah lewat malam"},
  };
  x.customSlotCount = (int)(sizeof(rows) / sizeof(rows[0]));
  for (int i = 0; i < x.customSlotCount && i < CONFIG_CUSTOM_SLOT_MAX; i++) {
    x.customSlots[i].hour = rows[i].h;
    x.customSlots[i].minute = rows[i].m;
    x.customSlots[i].warnBefore = rows[i].wb;
    configSafeCopy(x.customSlots[i].text, sizeof(x.customSlots[i].text),
                   rows[i].t);
  }
}

inline ConfigAnnounce defaultAnnounceConfig() {
  ConfigAnnounce x = {};
  x.prayer = true;
  x.custom = false;
  x.everyMinute = false;
  x.everyQuarter = true;
  x.nextPrayerPeriodMin = 5;
  x.quarterHourBeep = false;
  x.hourlyBell      = true;
  configAnnouncePrayerDefaults(x.prayerSlots);
  configAnnounceCustomDefaults(x);
  return x;
}

// ================================================================
// BACA / TULIS WIFI
// ================================================================
inline ConfigWiFi getWiFiConfig() {
  ConfigWiFi w = defaultWiFiConfig();
  JsonDocument doc;
  if (!configTryLoadJsonDoc(CONFIG_PATH_WIFI, "wifi.json parse gagal", doc))
    return w;

  const char *s;
  if (!doc["ssid"].isNull()) {
    s = doc["ssid"];
    if (s)
      configSafeCopy(w.ssid, sizeof(w.ssid), s);
  }
  if (!doc["password"].isNull()) {
    s = doc["password"];
    if (s)
      configSafeCopy(w.password, sizeof(w.password), s);
  }
  if (!doc["hostname"].isNull()) {
    s = doc["hostname"];
    if (s)
      configSafeCopy(w.hostname, sizeof(w.hostname), s);
  }

  return w;
}

inline bool saveWiFiConfig(const ConfigWiFi &cfg) {
  JsonDocument doc;
  doc["ssid"] = cfg.ssid;
  doc["password"] = cfg.password;
  doc["hostname"] = cfg.hostname;

  return configWriteJsonFile("wifi", CONFIG_PATH_WIFI, doc);
}

// ================================================================
// BACA / TULIS TAKWIM (URL + zon)
// ================================================================
inline ConfigTakwim getTakwimConfig() {
  ConfigTakwim t = defaultTakwimConfig();
  JsonDocument doc;
  if (!configTryLoadJsonDoc(CONFIG_PATH_TAKWIM, "takwim.json parse gagal", doc))
    return t;

  const char *s;
  if (!doc["url"].isNull()) {
    s = doc["url"];
    if (s)
      configSafeCopy(t.url, sizeof(t.url), s);
  }
  if (!doc["zone"].isNull()) {
    s = doc["zone"];
    if (s)
      configSafeCopy(t.zone, sizeof(t.zone), s);
  }

  return t;
}

inline bool saveTakwimConfig(const ConfigTakwim &cfg) {
  JsonDocument doc;
  doc["url"] = cfg.url;
  doc["zone"] = cfg.zone;

  return configWriteJsonFile("takwim", CONFIG_PATH_TAKWIM, doc);
}

// ================================================================
// BACA / TULIS AUDIO (volume 0..21, tts_lang)
// ================================================================
inline ConfigAudio getAudioConfig() {
  ConfigAudio a = defaultAudioConfig();
  JsonDocument doc;
  if (!configTryLoadJsonDoc(CONFIG_PATH_AUDIO, "audio.json parse gagal", doc))
    return a;

  if (!doc["volume"].isNull())
    a.volume = doc["volume"].as<int>();
  if (!doc["tts_lang"].isNull()) {
    const char *s = doc["tts_lang"];
    if (s)
      configSafeCopy(a.ttsLang, sizeof(a.ttsLang), s);
  }
  return a;
}

inline bool saveAudioConfig(const ConfigAudio &cfg) {
  JsonDocument doc;
  doc["volume"] = cfg.volume;
  doc["tts_lang"] = cfg.ttsLang;

  return configWriteJsonFile("audio", CONFIG_PATH_AUDIO, doc);
}

// ================================================================
// BACA / TULIS ANNOUNCE
// ================================================================
inline ConfigAnnounce getAnnounceConfig() {
  ConfigAnnounce x = defaultAnnounceConfig();
  JsonDocument doc;
  if (!configTryLoadJsonDoc(CONFIG_PATH_ANNOUNCE, "announce.json parse gagal",
                            doc))
    return x;

  if (!doc["prayer"].isNull())
    x.prayer = doc["prayer"].as<bool>();
  if (!doc["custom"].isNull())
    x.custom = doc["custom"].as<bool>();
  if (!doc["every_minute"].isNull())
    x.everyMinute = doc["every_minute"].as<bool>();
  if (!doc["every_quarter"].isNull())
    x.everyQuarter = doc["every_quarter"].as<bool>();
  if (!doc["next_prayer_period_min"].isNull())
    x.nextPrayerPeriodMin = doc["next_prayer_period_min"].as<int>();
  if (!doc["quarter_hour_beep"].isNull())
    x.quarterHourBeep = doc["quarter_hour_beep"].as<bool>();
  if (!doc["hourly_bell"].isNull())
    x.hourlyBell = doc["hourly_bell"].as<bool>();

  JsonArray ps = doc["prayer_slots"].as<JsonArray>();
  if (!ps.isNull()) {
    for (size_t i = 0; i < CONFIG_PRAYER_SLOT_COUNT && i < ps.size(); i++) {
      JsonObject o = ps[i];
      if (!o["announce"].isNull()) {
        const char *s = o["announce"];
        if (s)
          configSafeCopy(x.prayerSlots[i].announce,
                         sizeof(x.prayerSlots[i].announce), s);
      }
      if (!o["warn_before"].isNull())
        x.prayerSlots[i].warnBefore = o["warn_before"].as<int>();
      if (!o["warn_after"].isNull())
        x.prayerSlots[i].warnAfterSec = o["warn_after"].as<int>();
    }
  }

  JsonArray cs = doc["custom_slots"].as<JsonArray>();
  if (!cs.isNull()) {
    int n = (int)cs.size();
    if (n > CONFIG_CUSTOM_SLOT_MAX)
      n = CONFIG_CUSTOM_SLOT_MAX;
    x.customSlotCount = n;
    for (int i = 0; i < n; i++) {
      JsonObject o = cs[i];
      if (!o["hour"].isNull())
        x.customSlots[i].hour = o["hour"].as<int>();
      if (!o["minute"].isNull())
        x.customSlots[i].minute = o["minute"].as<int>();
      if (!o["warn_before"].isNull())
        x.customSlots[i].warnBefore = o["warn_before"].as<int>();
      if (!o["text"].isNull()) {
        const char *s = o["text"];
        if (s)
          configSafeCopy(x.customSlots[i].text,
                         sizeof(x.customSlots[i].text), s);
      }
    }
  }

  return x;
}

inline bool saveAnnounceConfig(const ConfigAnnounce &cfg) {
  JsonDocument doc;
  doc["prayer"] = cfg.prayer;
  doc["custom"] = cfg.custom;
  doc["every_minute"] = cfg.everyMinute;
  doc["every_quarter"] = cfg.everyQuarter;
  doc["next_prayer_period_min"] = cfg.nextPrayerPeriodMin;
  doc["quarter_hour_beep"] = cfg.quarterHourBeep;
  doc["hourly_bell"]      = cfg.hourlyBell;

  JsonArray ps = doc["prayer_slots"].to<JsonArray>();
  for (int i = 0; i < CONFIG_PRAYER_SLOT_COUNT; i++) {
    JsonObject o = ps.add<JsonObject>();
    o["announce"] = cfg.prayerSlots[i].announce;
    o["warn_before"] = cfg.prayerSlots[i].warnBefore;
    o["warn_after"] = cfg.prayerSlots[i].warnAfterSec;
  }

  JsonArray cs = doc["custom_slots"].to<JsonArray>();
  for (int i = 0; i < cfg.customSlotCount && i < CONFIG_CUSTOM_SLOT_MAX;
       i++) {
    JsonObject o = cs.add<JsonObject>();
    o["hour"] = cfg.customSlots[i].hour;
    o["minute"] = cfg.customSlots[i].minute;
    o["warn_before"] = cfg.customSlots[i].warnBefore;
    o["text"] = cfg.customSlots[i].text;
  }

  return configWriteJsonFile("announce", CONFIG_PATH_ANNOUNCE, doc);
}

#endif
