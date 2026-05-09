#ifndef TAKWIM_MODULE_H
#define TAKWIM_MODULE_H

#include <Arduino.h>
#include <RTClib.h>
#include <SPIFFS.h>

// ================================================================
// TAKWIM MODULE
// ================================================================
// Baca takwim.txt dari SPIFFS (format JAKIM-style):
//
//   WLY01 - Kuala Lumpur, Putrajaya
//   HIJRI_DATA=2B75A5B654A...
//   01-01-2026 11-07-1447  6:06  7:18  13:19  16:42  19:17  20:31
//   02-01-2026 12-07-1447  6:07  7:18  13:20  16:42  19:17  20:32
//   ...
//
// 7 nilai: Subuh, Syuruk, Zohor, Asar, Maghrib, Isyak (6 waktu)
// ================================================================

struct TakwimDay {
  int gDay, gMonth, gYear;       // Tarikh Masihi
  int hDay, hMonth, hYear;       // Tarikh Hijri
  int subuh, syuruk, zohor;      // dalam minit (0..1439)
  int asar, maghrib, isyak;
  bool valid;
};

// Cache hari semasa — elak baca SPIFFS berulang
static TakwimDay todayTakwim = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false};
static char zoneName[40] = "";   // header zon (cth: "WLY01 - Kuala Lumpur")

// ================================================================
// HELPER: Parse "H:MM" atau "HH:MM" → minit
// ================================================================
static int _parseHM(const char *s) {
  int h = 0, m = 0;
  if (sscanf(s, "%d:%d", &h, &m) != 2) return -1;
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

// ================================================================
// HELPER: Trim whitespace
// ================================================================
static void _trim(String &s) {
  s.trim();
}

// ================================================================
// Baca header zon (baris pertama) — sekali sahaja masa boot
// ================================================================
bool readTakwimZoneName() {
  File f = SPIFFS.open("/takwim.txt", "r");
  if (!f) return false;

  String line = f.readStringUntil('\n');
  _trim(line);
  f.close();

  if (line.length() == 0 || line.length() >= sizeof(zoneName)) {
    zoneName[0] = '\0';
    return false;
  }

  strncpy(zoneName, line.c_str(), sizeof(zoneName) - 1);
  zoneName[sizeof(zoneName) - 1] = '\0';
  return true;
}

// ================================================================
// CORE: Cari & parse baris untuk tarikh tertentu
// ================================================================
bool loadTakwimForDate(int gd, int gm, int gy) {
  // ── Cache hit? ──
  if (todayTakwim.valid &&
      todayTakwim.gDay == gd &&
      todayTakwim.gMonth == gm &&
      todayTakwim.gYear == gy) {
    return true;
  }

  File f = SPIFFS.open("/takwim.txt", "r");
  if (!f) {
    Serial.println(F("Takwim: gagal buka /takwim.txt"));
    todayTakwim.valid = false;
    return false;
  }

  // Format target di awal baris: "DD-MM-YYYY"
  char target[12];
  snprintf(target, sizeof(target), "%02d-%02d-%04d", gd, gm, gy);

  String line;
  bool found = false;

  while (f.available()) {
    line = f.readStringUntil('\n');
    _trim(line);

    // Skip header, baris pendek, dan HIJRI_DATA
    if (line.length() < 30) continue;
    if (line.startsWith("HIJRI_DATA")) continue;

    // Cari baris yang bermula dengan tarikh sasaran
    if (!line.startsWith(target)) continue;

    // ── Parse baris ──
    // Format: "01-01-2026 11-07-1447\t6:06\t7:18\t13:19\t16:42\t19:17\t20:31"
    int hd, hm, hy;
    char s1[8], s2[8], s3[8], s4[8], s5[8], s6[8];

    int n = sscanf(line.c_str(),
                   "%*d-%*d-%*d %d-%d-%d %7s %7s %7s %7s %7s %7s",
                   &hd, &hm, &hy, s1, s2, s3, s4, s5, s6);

    if (n != 9) {
      Serial.printf("Takwim: parse gagal (%d field): %s\n", n, line.c_str());
      continue;
    }

    int v1 = _parseHM(s1), v2 = _parseHM(s2), v3 = _parseHM(s3);
    int v4 = _parseHM(s4), v5 = _parseHM(s5), v6 = _parseHM(s6);

    if (v1 < 0 || v2 < 0 || v3 < 0 || v4 < 0 || v5 < 0 || v6 < 0) {
      Serial.printf("Takwim: masa tidak sah: %s\n", line.c_str());
      continue;
    }

    todayTakwim.gDay   = gd;
    todayTakwim.gMonth = gm;
    todayTakwim.gYear  = gy;
    todayTakwim.hDay   = hd;
    todayTakwim.hMonth = hm;
    todayTakwim.hYear  = hy;
    todayTakwim.subuh   = v1;
    todayTakwim.syuruk  = v2;
    todayTakwim.zohor   = v3;
    todayTakwim.asar    = v4;
    todayTakwim.maghrib = v5;
    todayTakwim.isyak   = v6;
    todayTakwim.valid = true;
    found = true;
    break;
  }
  f.close();

  // ── Log selepas tutup file (guna data dari struct, bukan local var) ──
  if (found) {
    Serial.printf("Takwim: %02d/%02d/%d (%d-%d-%dH) — Sub:%02d:%02d Zhr:%02d:%02d "
                  "Asr:%02d:%02d Mgb:%02d:%02d Isy:%02d:%02d\n",
                  todayTakwim.gDay, todayTakwim.gMonth, todayTakwim.gYear,
                  todayTakwim.hDay, todayTakwim.hMonth, todayTakwim.hYear,
                  todayTakwim.subuh   / 60, todayTakwim.subuh   % 60,
                  todayTakwim.zohor   / 60, todayTakwim.zohor   % 60,
                  todayTakwim.asar    / 60, todayTakwim.asar    % 60,
                  todayTakwim.maghrib / 60, todayTakwim.maghrib % 60,
                  todayTakwim.isyak   / 60, todayTakwim.isyak   % 60);
  } else {
    Serial.printf("Takwim: %s tidak dijumpai dalam /takwim.txt\n", target);
    todayTakwim.valid = false;
  }
  return found;
}

// ================================================================
// Auto-refresh bila tarikh berubah (panggil dalam loop)
// ================================================================
void refreshTakwimIfNeeded(DateTime now) {
  if (!todayTakwim.valid ||
      todayTakwim.gDay   != now.day() ||
      todayTakwim.gMonth != now.month() ||
      todayTakwim.gYear  != now.year()) {
    loadTakwimForDate(now.day(), now.month(), now.year());
  }
}

// ================================================================
// Init — panggil sekali masa boot (selepas SPIFFS.begin)
// ================================================================
bool initTakwim(DateTime now) {
  if (!readTakwimZoneName()) {
    Serial.println(F("Takwim: zone header tidak dijumpai"));
  } else {
    Serial.printf("Takwim: zon = %s\n", zoneName);
  }
  return loadTakwimForDate(now.day(), now.month(), now.year());
}

#endif