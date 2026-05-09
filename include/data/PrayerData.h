#ifndef PRAYER_DATA_H
#define PRAYER_DATA_H

#include "TakwimModule.h"

// ================================================================
// SUMBER TUNGGAL DATA WAKTU SOLAT
// Data sebenar dari /takwim.txt (di-load oleh TakwimModule)
// Array ini di-update oleh syncPrayersFromTakwim()
// ================================================================

struct PrayerSlot {
  const char *name;
  const char *announce;
  int hour;
  int minute;
  int warnBefore;
};

static PrayerSlot prayers[] = {
  {"Subuh",  "It is now Subooh prayer time",    0, 0, 30},
  {"Syuruk", "It is now Shooroock time",         0, 0,  0},
  {"Zohor",  "It is now Zohor prayer time",     0, 0, 30},
  {"Asar",   "It is now Asarr prayer time",     0, 0, 30},
  {"Mgrb",   "It is now Mughrib prayer time",   0, 0, 30},
  {"Isyak",  "It is now Ishaa prayer time",     0, 0, 30},
};
static const int PRAYER_COUNT = sizeof(prayers) / sizeof(PrayerSlot);

// ================================================================
// Sync prayers[] dari todayTakwim
// Panggil selepas loadTakwimForDate() berjaya
// ================================================================
void syncPrayersFromTakwim() {
  if (!todayTakwim.valid) {
    Serial.println(F("Prayer: takwim tidak valid — guna nilai default"));
    return;
  }

  prayers[0].hour = todayTakwim.subuh   / 60;
  prayers[0].minute = todayTakwim.subuh   % 60;

  prayers[1].hour = todayTakwim.syuruk  / 60;
  prayers[1].minute = todayTakwim.syuruk  % 60;

  prayers[2].hour = todayTakwim.zohor   / 60;
  prayers[2].minute = todayTakwim.zohor   % 60;

  prayers[3].hour = todayTakwim.asar    / 60;
  prayers[3].minute = todayTakwim.asar    % 60;

  prayers[4].hour = todayTakwim.maghrib / 60;
  prayers[4].minute = todayTakwim.maghrib % 60;

  prayers[5].hour = todayTakwim.isyak   / 60;
  prayers[5].minute = todayTakwim.isyak   % 60;

  Serial.printf("Prayer: synced — Sub %02d:%02d, Zhr %02d:%02d, "
                "Asr %02d:%02d, Mgb %02d:%02d, Isy %02d:%02d\n",
                prayers[0].hour, prayers[0].minute,
                prayers[2].hour, prayers[2].minute,
                prayers[3].hour, prayers[3].minute,
                prayers[4].hour, prayers[4].minute,
                prayers[5].hour, prayers[5].minute);
}

#endif