#include "Audio.h"
#include "config.h"
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RTClib.h>
#include <WiFi.h>
#include <Wire.h>

// ================================================================
// DEFINISI GLOBAL
// ================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RTC_DS3231 rtc;
Audio audio;
String audioStatus = "IDLE";

// ================================================================
// MODUL
// ================================================================
#include "AnnounceModule.h"
#include "AudioModule.h"
#include "BeepModule.h"
#include "DisplayModule.h"
#include "TimeModule.h"
#include "TakwimModule.h"

// ================================================================
// Pengumuman TTS pembuka masa boot (Perlukan WiFi untuk Google TTS)
// ================================================================
static void announceBootDateTimeRtc() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("Boot: pengumuman masa dilangkau (tiada WiFi)"));
    return;
  }

  static const char *const DAYS[] = {
      "Sunday", "Monday", "Tuesday", "Wednesday",
      "Thursday", "Friday", "Saturday"};
  static const char *const MONTHS[] = {
      "",
      "January", "February", "March", "April",
      "May", "June", "July", "August",
      "September", "October", "November", "December"};

  DateTime t = rtc.now();

  uint8_t dow = t.dayOfTheWeek();
  if (dow >= 7)
    dow = 0;

  uint8_t mon = t.month();
  if (mon < 1 || mon > 12)
    mon = 1;

  int h = t.hour(), m = t.minute();
  int h12 = (h % 12 == 0) ? 12 : h % 12;
  const char *ap = (h < 12) ? "AM" : "PM";

  char line[124];
  snprintf(line, sizeof(line),
           "Its now %d:%02d %s, %s, %s %d, %d",
           h12, m, ap, DAYS[dow], MONTHS[mon], (int)t.day(), (int)t.year());
  enqueueSpeech(line);
}

// ================================================================
// SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n=== BOOT ==="));

  initDisplay();
  initTime();

  showSplashLogo(2000);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 15000) {
      showWiFiError();
      delay(2000);
      break;
    }
    int progress = (int)((millis() - wifiStart) * 90 / 15000) + 5;
    showBootStatus("Connecting WiFi...", progress);
    delay(100);
  }

  // NTP
  if (rtcNeedsSync()) {
    if (WiFi.status() == WL_CONNECTED) {
      showBootStatus("Syncing time...", 95);
      if (!syncNTP()) {
        showBootStatus("NTP failed...", 95);
        delay(1500);
      }
    } else {
      showRtcWarning();
      delay(2500);
    }
  } else {
    Serial.println(F("RTC OK - NTP sync dilangkau"));
  }

  // Audio init & task (perlu sebelum initBeeps — ujian beep guna enqueueSpeech)
  initAudio();
  xTaskCreatePinnedToCore(AudioLoopTask, "AudioTask", 32768, NULL, 1, NULL, 0);

  // SPIFFS + WAV beep (jana hanya jika belum ada) + ujian bunyi boot
  showBootStatus("Loading beep...", 97);
  initBeeps();

  // Takwim
  DateTime nowBoot = rtc.now();
  initTakwim(nowBoot);
  syncPrayersFromTakwim();

  announceBootDateTimeRtc();

  showBootStatus("System Ready!", 100);
  delay(500);
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  DateTime now = rtc.now();

  static int lastDay = -1;
  if (lastDay == -1) {
    lastDay = now.day();   // first iteration — set & skip sync
  } else if (now.day() != lastDay) {
    refreshTakwimIfNeeded(now);
    syncPrayersFromTakwim();
    lastDay = now.day();
  }

  runAnnounceModule(now);
  runDisplay(now);

  delay(10);
}