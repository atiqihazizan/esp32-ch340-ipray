#ifndef TIME_MODULE_H
#define TIME_MODULE_H

#include "config.h"
#include <RTClib.h>
#include <WiFi.h>
#include <Wire.h>

extern RTC_DS3231 rtc;
// Diset dalam initTime() — false jika DS3231 tidak menjawab; masa ikut NTP → sistem ESP.
extern bool rtcHardwareOk;

// ================================================================
// INIT RTC — satu percubaan pantas; tanpa imbas I2C panjang
// ================================================================
bool initTime() {
  rtcHardwareOk = rtc.begin(&Wire);
  if (rtcHardwareOk) {
    Serial.println(F("RTC: OK"));
  } else {
    Serial.println(
        F("RTC: tiada jawapan — terus guna NTP / masa sistem (ESP)"));
  }
  return rtcHardwareOk;
}

// ================================================================
// CHECK — Adakah RTC perlu sync?
//
// Tanpa modul RTC: guna semakan rtcTimeLooksValid() + NTP, bukan lostPower.
// ================================================================
bool rtcNeedsSync() {
  if (!rtcHardwareOk)
    return false;
  return rtc.lostPower();
}

// ================================================================
// Semakan julat — RTC atau masa sistem (lepas configTime / NTP)
// ================================================================
bool rtcTimeLooksValid() {
  if (rtcHardwareOk) {
    DateTime t = rtc.now();
    uint16_t y = t.year();
    uint8_t mo = t.month(), d = t.day();
    if (y < 2020 || y > 2099)
      return false;
    if (mo < 1 || mo > 12)
      return false;
    if (d < 1 || d > 31)
      return false;
    return true;
  }

  struct tm ti;
  if (!getLocalTime(&ti, 0))
    return false;
  int y = ti.tm_year + 1900;
  int mo = ti.tm_mon + 1;
  int d = ti.tm_mday;
  if (y < 2020 || y > 2099)
    return false;
  if (mo < 1 || mo > 12)
    return false;
  if (d < 1 || d > 31)
    return false;
  return true;
}

inline bool rtcShouldTryNtpSync() {
  if (!rtcHardwareOk)
    return !rtcTimeLooksValid();
  return rtcNeedsSync() || !rtcTimeLooksValid();
}

// ================================================================
// Masa “sekarang” — DS3231 jika ada, jika tidak struct tm selepas NTP
// ================================================================
inline DateTime clockNowDateTime() {
  if (rtcHardwareOk)
    return rtc.now();
  struct tm ti;
  if (getLocalTime(&ti, 0))
    return DateTime(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                    ti.tm_hour, ti.tm_min, ti.tm_sec);
  return DateTime(2000, 1, 1, 0, 0, 0);
}

// ================================================================
// SYNC MASA DARI NTP → RTC (atau hanya sistem jika tiada RTC)
//
// Return: true  = berjaya sync
//         false = NTP timeout / gagal
// ================================================================
bool syncNTP() {
  Serial.println(F("NTP: Memulakan sync..."));

  configTime(GMT_OFFSET, 0, "pool.ntp.org", "time.google.com");

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10000)) {
    Serial.println(F("NTP: Timeout / gagal"));
    return false;
  }

  if (rtcHardwareOk) {
    rtc.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
    ));
    delay(50);
  }

  if (!rtcTimeLooksValid()) {
    Serial.println(rtcHardwareOk
                       ? F("NTP: RTC masih tidak sah selepas adjust — semak I2C / RTC")
                       : F("NTP: masa sistem tidak sah selepas sync"));
    return false;
  }

  Serial.printf("NTP: Sync berjaya → %02d/%02d/%04d %02d:%02d:%02d%s\n",
                timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                rtcHardwareOk ? "" : " (tiada RTC — masa dalam ESP sahaja)");

  return true;
}

#endif
