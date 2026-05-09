#ifndef TIME_MODULE_H
#define TIME_MODULE_H

#include "config.h"
#include <RTClib.h>
#include <WiFi.h>

extern RTC_DS3231 rtc;

// ================================================================
// INIT RTC
// ================================================================
bool initTime() {
  if (!rtc.begin()) {
    Serial.println(F("RTC: Gagal dikesan"));
    return false;
  }
  Serial.println(F("RTC: OK"));
  return true;
}

// ================================================================
// CHECK — Adakah RTC perlu sync?
//
// rtc.lostPower() = true bermaksud:
//   • Pertama kali boot (belum pernah di-set)
//   • Bateri RTC habis / tertanggal
//   • Power loss tanpa bateri backup
//
// Jika false → masa RTC masih sah, skip NTP.
// ================================================================
bool rtcNeedsSync() {
  return rtc.lostPower();
}

// ================================================================
// SYNC MASA DARI NTP → RTC
//
// Return: true  = berjaya sync
//         false = NTP timeout / gagal
// ================================================================
bool syncNTP() {
  Serial.println(F("NTP: Memulakan sync..."));

  // Mulakan permintaan NTP (async)
  configTime(GMT_OFFSET, 0, "pool.ntp.org", "time.google.com");

  // Tunggu sehingga masa diterima (timeout 10 saat)
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10000)) {
    Serial.println(F("NTP: Timeout / gagal"));
    return false;
  }

  // Tulis ke RTC (ini juga clear flag lostPower)
  rtc.adjust(DateTime(
    timeinfo.tm_year + 1900,
    timeinfo.tm_mon + 1,
    timeinfo.tm_mday,
    timeinfo.tm_hour,
    timeinfo.tm_min,
    timeinfo.tm_sec
  ));

  Serial.printf("NTP: Sync berjaya → %02d/%02d/%04d %02d:%02d:%02d\n",
    timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  return true;
}

#endif