#include "Audio.h"
#include "config.h"
#include "data/ConfigModule.h"
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
bool rtcHardwareOk = false;
Audio audio;
String audioStatus = "IDLE";

// ================================================================
// MODUL
// ================================================================
#include "logic/AnnounceModule.h"
#include "core/AudioStorageModule.h"
#include "core/AudioModule.h"
#include "logic/BeepModule.h"
#include "core/DisplayModule.h"
#include "network/WiFiManager.h"
#include "core/TimeModule.h"
#include "data/TakwimModule.h"
#include "network/WebServer.h"

bool clockWebRebootSoon = false;

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

  DateTime t = clockNowDateTime();

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
  Serial.setTimeout(30);   // elak loop tergantung pada readStringUntil
  delay(200);
  Serial.println(F("\n=== BOOT ==="));

  if (!initConfigStorage()) {
    Serial.println(F("Boot: SPIFFS gagal — /config tidak tersedia, guna config.h sahaja "
                     "sehingga SPIFFS OK"));
  }

  initAudioStorage();
  initDisplay();      // WAJIB sebelum initTime() — Wire.begin() untuk bas I2C
  initTime();

  showSplashLogo(2000);

  ConfigWiFi wifiCfg = getWiFiConfig();
  wifiBootStaThenAp(wifiCfg);

  // NTP — guna rtcShouldTryNtpSync(): tanpa RTC perkakasan, rtcNeedsSync() sentiasa false
  // dan NTP tidak akan jalan; masa sistem/jisim tarikh jadi sampah (cth. bulan > 12).
  if (rtcShouldTryNtpSync()) {
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
    Serial.println(F("NTP: dilangkau — masa sudah nampak sah / RTC tidak rugi kuasa"));
  }

  // Audio init & task (perlu sebelum initBeeps — ujian beep guna enqueueSpeech)
  initAudio();

  ConfigAudio audioCfg = getAudioConfig();
  applyAudioRuntimeConfig(audioCfg.volume, audioCfg.ttsLang);

  xTaskCreatePinnedToCore(AudioLoopTask, "AudioTask", 32768, NULL, 1, NULL, 0);

  // SPIFFS + WAV beep (jana hanya jika belum ada) + ujian bunyi boot
  showBootStatus("Loading beep...", 97);
  initBeeps();

  ConfigAnnounce ancCfg = getAnnounceConfig();
  applyAnnounceRuntimeConfig(ancCfg);

  // Takwim — jangan guna rtc.now() jika tiada DS3231 (sampah I2C → 00-85-2165 dll)
  DateTime nowBoot = clockNowDateTime();
  initTakwim(nowBoot);
  syncPrayersFromTakwim();

  announceBootDateTimeRtc();

  clockWebServerBegin();

  showBootStatus("System Ready!", 100);
  delay(500);
}

// ================================================================
// LOOP
// ================================================================
void loop() {
  clockWebServerLoop();

  static unsigned long s_lastRbCfgMs = 0;
  static ConfigAnnounce s_anRb;
  static ConfigAudio    s_audRb;

  if (ttsCacheRebuildActive()) {
    unsigned long m = millis();
    if (s_lastRbCfgMs == 0 || m - s_lastRbCfgMs > 800) {
      s_lastRbCfgMs = m;
      s_anRb  = getAnnounceConfig();
      s_audRb = getAudioConfig();
    }
    ttsCacheRebuildPump(s_anRb, s_audRb);
  } else {
    s_lastRbCfgMs = 0;
  }

  if (clockWebRebootSoon) {
    delay(450);
    ESP.restart();
  }

  DateTime now = clockNowDateTime();

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

  // Arahan ujian beep — baca aksara demi aksara (elak timeout 30 ms pecahkan taipan)
  static String serialCmdLine;
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialCmdLine.length() == 0)
        continue;
      String cmd = serialCmdLine;
      serialCmdLine = "";
      cmd.trim();
      cmd.toUpperCase();

      if (cmd == "DOUBLEBEEP") {
        beepDouble();
        Serial.println(F("OK: DOUBLEBEEP"));
      } else if (cmd == "PRAYERBEEP") {
        beepPrayer();
        Serial.println(F("OK: PRAYERBEEP"));
      } else if (cmd == "WARNINGBEEP") {
        beepWarning();
        Serial.println(F("OK: WARNINGBEEP"));
      } else {
        Serial.println(F("Arahan: DOUBLEBEEP | PRAYERBEEP | WARNINGBEEP"));
      }
    } else if (serialCmdLine.length() < 40) {
      serialCmdLine += c;
    }
  }
}