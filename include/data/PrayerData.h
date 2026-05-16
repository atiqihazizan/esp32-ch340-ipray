#ifndef PRAYER_DATA_H
#define PRAYER_DATA_H

#include "TakwimModule.h"

// ================================================================
// SUMBER TUNGGAL DATA WAKTU SOLAT
// Data sebenar dari /takwim.txt (di-load oleh TakwimModule)
// Array ini di-update oleh syncPrayersFromTakwim()
// ================================================================

// Panjang teks TTS selari dengan CONFIG_ANNOUNCE_TEXT_LEN dalam ConfigModule
#define PRAYER_ANNOUNCE_LEN 128

struct PrayerSlot {
  const char *name;
  char announce[PRAYER_ANNOUNCE_LEN];
  int hour;
  int minute;
  int warnBefore;
  int warnAfterSec; // peringatan selepas waktu masuk (saat, 0 = mati)
};

static PrayerSlot prayers[] = {
    {"Subuh",  {0}, 0, 0, 600,  300},
    {"Syuruk", {0}, 0, 0,  0,    0},
    {"Zohor",  {0}, 0, 0, 300,  300},
    {"Asar",   {0}, 0, 0, 300,  300},
    {"Mgrb",   {0}, 0, 0, 300,  300},
    {"Isyak",  {0}, 0, 0, 180,  300},
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