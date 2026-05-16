#ifndef DISPLAYMODULE_H
#define DISPLAYMODULE_H

#include "SlideAnimUtil.h"
#include "config.h"
#include "data/ConfigModule.h"
#include "data/PrayerData.h"
#include "logic/BeepModule.h"
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <Wire.h>

extern Adafruit_SSD1306 display;
extern String audioStatus;

// ─── ENUM LAYOUT ─────────────────────────────────────────
enum ScreenLayout { LAYOUT_HOME_PRAYER, LAYOUT_TAKWIM };

ScreenLayout activeLayout = LAYOUT_HOME_PRAYER;

// Panel bawah A/B/C — flip setiap ~4s (sama pola seperti backup DisplayModule)
static constexpr uint32_t HOME_PANEL_FLIP_MS = 4000UL;

static int homePanelIdx = 0;
static uint32_t homeLastPanelFlip = 0;

// ─── STRUCTS ─────────────────────────────────────────────
struct HijriDate {
  int day, month, year;
};

// Digunakan oleh DisplayModule — definisi dalam AnnounceModule.h
extern int announceNextPrayerPeriodMin;

// ─── WAKTU SOLAT ─────────────────────────────────────────
// Pulangkan solat "seterusnya". Jika waktu baru masuk dalam tempoh period,
// waktu itu masih dianggap semasa (diff boleh negatif — diselesaikan di Panel A).
const char *getNextPrayer(int h, int m, int &outH, int &outM) {
  int nowMin = h * 60 + m;
  int period = announceNextPrayerPeriodMin;
  for (int i = 0; i < PRAYER_COUNT; i++) {
    int pt = prayers[i].hour * 60 + prayers[i].minute;
    if (pt + period > nowMin) {
      outH = prayers[i].hour;
      outM = prayers[i].minute;
      return prayers[i].name;
    }
  }
  outH = prayers[0].hour;
  outM = prayers[0].minute;
  return prayers[0].name;
}

// ─── KONVERSI GREGORIAN → HIJRI ──────────────────────────
HijriDate toHijri(int gd, int gm, int gy) {
  int a = (14 - gm) / 12, y = gy + 4800 - a, m = gm + 12 * a - 3;
  long jdn = (long)gd + (153L * m + 2) / 5 + 365L * y + y / 4 - y / 100 +
             y / 400 - 32045;
  long l = jdn - 1948440L + 10632;
  long n = (l - 1) / 10631;
  l = l - 10631L * n + 354;
  long j =
      (10985 - l) / 5316 * (50L * l / 17719) + (l / 5670) * (43L * l / 15238);
  l = l - (30 - j) * (15719L * j / 50) - (j * 16) / (15238L * j / 43) + 29;
  HijriDate h;
  h.month = (int)(24L * l / 709);
  h.day = (int)(l - 709L * h.month / 24);
  h.year = (int)(30 * n + j - 30);
  return h;
}

const char *hijriMonthShort(int m) {
  const char *n[] = {"Mhrm",  "Safar", "R.Awl", "R.Akh", "J.Awl", "J.Akh",
                     "Rejab", "Sybn",  "Rmdn",  "Sywal", "Z.Qed", "Z.Hjj"};
  return (m >= 1 && m <= 12) ? n[m - 1] : "?";
}

// ─── INIT ────────────────────────────────────────────────
void initDisplay() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(100000); // 100 kHz — lebih stabil jika bas I2C bising / tarik atas lemah
  delay(20);

  bool ok = false;
  for (int attempt = 0; attempt < 3 && !ok; attempt++) {
    if (attempt > 0)
      delay(80);
    ok = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  }

  if (!ok) {
    Serial.println(F("SSD1306 gagal — semak SDA/SCL, VCC, GND"));
  }
  display.clearDisplay();
  display.display();
}

// ─── STATUS BAR ──────────────────────────────────────────
inline void printOledWifiIpStatus() {
  String s;
  if (WiFi.status() == WL_CONNECTED) {
    s = WiFi.localIP().toString();
  } else {
    wifi_mode_t wm = WiFi.getMode();
    if (wm == WIFI_MODE_AP || wm == WIFI_MODE_APSTA)
      s = WiFi.softAPIP().toString();
    else
      s = "WiFi--";
  }
  const int maxCh = 11;
  if ((int)s.length() > maxCh)
    s = s.substring((unsigned)(s.length() - maxCh));
  display.print(s);
}

// ─── BOOT: Spinner ───────────────────────────────────────
static int spinnerIdx = 0;
static const char spinnerChars[] = {'|', '/', '-', '\\'};

static void drawSpinner(int x, int y) {
  static uint32_t lastSpin = 0;
  if (millis() - lastSpin > 150) {
    spinnerIdx = (spinnerIdx + 1) % 4;
    lastSpin = millis();
  }
  display.setCursor(x, y);
  display.print(spinnerChars[spinnerIdx]);
}

void drawProgressBar(int progress) {
  display.drawRect(0, 45, 128, 10, SSD1306_WHITE);
  int fill = progress * (128 - 4) / 100;
  display.fillRect(2, 47, fill, 6, SSD1306_WHITE);
}

// ─── BOOT: Splash ────────────────────────────────────────
void showSplashLogo(int durationMs = 2000) {
  const int cx = 64, cy = 26;
  display.clearDisplay();
  display.drawCircle(cx, cy, 18, SSD1306_WHITE);
  display.drawCircle(cx, cy, 12, SSD1306_WHITE);
  display.fillCircle(cx, cy, 4, SSD1306_WHITE);
  display.drawLine(cx, cy, cx, cy - 10, SSD1306_WHITE);
  display.drawLine(cx, cy, cx + 8, cy, SSD1306_WHITE);
  const char *title = "MY DEVICE";
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
  display.setCursor(64 - (int)(strlen(title) * 6 / 2), 55);
  display.print(title);
  display.display();
  delay(durationMs);
}

// ─── BOOT: Status ────────────────────────────────────────
void showBootStatus(String msg, int progress) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 5);
  display.println("SYSTEM INITIALIZING");
  display.drawLine(0, 15, 128, 15, SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print(msg);
  drawSpinner(115, 25);
  drawProgressBar(progress);
  display.display();
}

// ─── BOOT: RTC Warning ───────────────────────────────────
void showRtcWarning() {
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(8, 3);
  display.print("! INVALID TIME !");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 15);
  display.print("RTC was reset.");
  display.setCursor(0, 25);
  display.print("No WiFi for sync.");
  display.drawLine(0, 36, 128, 36, SSD1306_WHITE);
  display.setCursor(0, 40);
  display.print("Time may be wrong");
  display.setCursor(0, 51);
  display.print("until next sync.");
  display.display();
}

// ─── BOOT: WiFi Error ────────────────────────────────────
void showWiFiError() {
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(20, 3);
  display.print("! WIFI FAILED !");
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 15);
  display.print("No WiFi connection.");
  display.setCursor(0, 25);
  display.print("TTS unavailable.");
  display.drawLine(0, 36, 128, 36, SSD1306_WHITE);
  display.setCursor(0, 40);
  display.print("Offline mode.");
  display.setCursor(0, 51);
  display.print("Clock still runs.");
  display.display();
}

// ─── HOLD & BEEP STATE ───────────────────────────────────
static int _activePrayerIdx = -1;
static uint32_t _prayerStartMs = 0;
static bool _prayerHoldDone = false;

static int getCurrentPrayerIdx(int h, int m) {
  int nowMin = h * 60 + m;
  for (int i = 0; i < PRAYER_COUNT; i++) {
    int thisStart = prayers[i].hour * 60 + prayers[i].minute;
    int nextStart = (i + 1 < PRAYER_COUNT)
                        ? prayers[i + 1].hour * 60 + prayers[i + 1].minute
                        : 24 * 60;
    if (nowMin >= thisStart && nowMin < nextStart)
      return i;
  }
  return -1;
}

// ─── LAYOUT HOME PRAYER ──────────────────────────────────
void runOledHomePrayer(DateTime now) {
  static SlideField fHour, fMin, fSec;

  // Date flipflop Masihi/Hijri setiap 4s
  static bool showHijri = false;
  static uint32_t lastShowHijri = 0;
  if (millis() - lastShowHijri > 4000) {
    showHijri = !showHijri;
    lastShowHijri = millis();
  }

  // Panel flipflop A/B/C setiap 4s (0=NextPrayer, 1=AllPrayers, 2=Network)
  if (millis() - homeLastPanelFlip > HOME_PANEL_FLIP_MS) {
    homePanelIdx = (homePanelIdx + 1) % 3;
    homeLastPanelFlip = millis();
  }

  // Hostname cache
  static char cachedHostname[33] = {0};
  static bool hostnameInit = false;
  if (!hostnameInit) {
    ConfigWiFi w = getWiFiConfig();
    strncpy(cachedHostname, w.hostname, 32);
    cachedHostname[32] = 0;
    if (cachedHostname[0] == 0)
      strncpy(cachedHostname, "my-clock", 32);
    hostnameInit = true;
  }

  // Hold 1 minit + double beep
  int curIdx = getCurrentPrayerIdx(now.hour(), now.minute());
  if (curIdx != _activePrayerIdx) {
    _activePrayerIdx = curIdx;
    _prayerStartMs = millis();
    _prayerHoldDone = false;
  }
  if (!_prayerHoldDone && _activePrayerIdx >= 0) {
    if (millis() - _prayerStartMs >= 60000UL) {
      beepDouble();
      _prayerHoldDone = true;
      homePanelIdx = 0;
      homeLastPanelFlip = millis();
    }
  }

  // ── Lukis ──
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // JAM textSize 2
  const int CLOCKY = 4;
  const int XHH = 16, XC1 = XHH + 24;
  const int XMM = XC1 + 12, XC2 = XMM + 24;
  const int XSS = XC2 + 12;
  slideDrawField(fHour, now.hour(), XHH, CLOCKY, 2, 2);
  slideDrawField(fMin, now.minute(), XMM, CLOCKY, 2, 2);
  slideDrawField(fSec, now.second(), XSS, CLOCKY, 2, 2);
  slideDrawSeparator(":", XC1, CLOCKY, 2);
  slideDrawSeparator(":", XC2, CLOCKY, 2);

  // TARIKH Masihi/Hijri
  display.setTextSize(1);
  if (!showHijri) {
    char ds[11];
    sprintf(ds, "%02d/%02d/%04d", now.day(), now.month(), now.year());
    display.setCursor(64 - (int)(strlen(ds) * 3), 24);
    display.print(ds);
  } else {
    HijriDate h;
    if (todayTakwim.valid) {
      h.day = todayTakwim.hDay;
      h.month = todayTakwim.hMonth;
      h.year = todayTakwim.hYear;
    } else {
      h = toHijri(now.day(), now.month(), now.year());
    }
    char hs[20];
    sprintf(hs, "%d %s %dH", h.day, hijriMonthShort(h.month), h.year);
    display.setCursor(64 - (int)(strlen(hs) * 3), 24);
    display.print(hs);
  }

  display.drawLine(0, 34, 128, 34, SSD1306_WHITE);

  // ── BOTTOM PANEL ──
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (homePanelIdx == 0) {
    // PANEL A: Next Prayer / Waktu Semasa (dalam tempoh grace)
    int nextH, nextM;
    const char *pName = getNextPrayer(now.hour(), now.minute(), nextH, nextM);
    int diff = nextH * 60 + nextM - now.hour() * 60 - now.minute();
    // diff negatif = waktu baru masuk (dalam tempoh grace announceNextPrayerPeriodMin)
    bool justEntered = (diff <= 0 && diff >= -announceNextPrayerPeriodMin);
    display.setCursor(0, 38);
    display.printf("Waktu : %-6s %02d:%02d", pName, nextH, nextM);
    display.setCursor(0, 50);
    if (justEntered) {
      display.printf("Masuk : %d min lalu", -diff);
    } else {
      if (diff < 0) diff += 1440;
      display.printf("Masa  : %dj %02dm lagi", diff / 60, diff % 60);
    }

  } else if (homePanelIdx == 1) {
    // PANEL B: Semua waktu solat
    int nowMin = now.hour() * 60 + now.minute();
    int nextIdx = -1;
    for (int i = 0; i < PRAYER_COUNT; i++) {
      if (prayers[i].hour * 60 + prayers[i].minute > nowMin) {
        nextIdx = i;
        break;
      }
    }
    if (nextIdx == -1)
      nextIdx = 0;

    struct PrayerCell {
      int prayerIdx, col, row;
    };
    static const PrayerCell layout6[] = {
        {0, 0, 0}, // Subuh   kiri atas
        {1, 1, 0}, // Syuruk  kanan bawah
        {2, 1, 1}, // Zohor   kanan atas
        {3, 0, 1}, // Asar    kiri tengah
        {4, 1, 2}, // Maghrib kanan tengah
        {5, 0, 2}, // Isyak   kiri bawah
    };
    const int CELLW = 64, CELLH = 9, YBASE = 37;

    for (int i = 0; i < 6; i++) {
      const PrayerCell &c = layout6[i];
      int pi = c.prayerIdx;
      int x = c.col * CELLW;
      int y = YBASE + c.row * CELLH;
      bool isNext = (pi == nextIdx);

      if (isNext) {
        display.fillRect(x, y - 1, CELLW, CELLH, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }
      const char *full = prayers[pi].name;
      char shortName[4] = {full[0], full[1] ? full[1] : ' ',
                           full[2] ? full[2] : ' ', 0};
      char buf[16];
      snprintf(buf, sizeof(buf), "%s %d:%02d", shortName, prayers[pi].hour,
               prayers[pi].minute);
      display.setCursor(x + 1, y);
      display.print(buf);
      display.setTextColor(SSD1306_WHITE);
    }

  } else {
    // PANEL C: Network info
    display.setCursor(0, 38);
    display.print("IP   : ");
    if (WiFi.status() == WL_CONNECTED) {
      display.print(WiFi.localIP().toString());
    } else {
      wifi_mode_t wm = WiFi.getMode();
      if (wm == WIFI_MODE_AP || wm == WIFI_MODE_APSTA)
        display.print(WiFi.softAPIP().toString());
      else
        display.print("--");
    }
    display.setCursor(0, 50);
    display.printf("Host : %s", cachedHostname);
  }

  display.display();
}

// ─── LAYOUT TAKWIM ───────────────────────────────────────
void runOledTakwim(DateTime now) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header inverted
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  const char *mNames[] = {"Januari",   "Februari", "Mac",      "April",
                          "Mei",       "Jun",      "Julai",    "Ogos",
                          "September", "Oktober",  "November", "Disember"};
  char title[18];
  sprintf(title, "%s %d", mNames[now.month() - 1], now.year());
  display.setCursor(64 - (int)(strlen(title) * 3), 3);
  display.print(title);

  // Nama hari
  display.setTextColor(SSD1306_WHITE);
  const char *dh[] = {"Ah", "Is", "Se", "Ra", "Kh", "Ju", "Sa"};
  for (int i = 0; i < 7; i++) {
    display.setCursor(i * 18 + 3, 14);
    display.print(dh[i]);
  }
  display.drawLine(0, 22, 128, 22, SSD1306_WHITE);

  // Grid tarikh
  int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int yr = now.year();
  if (yr % 4 == 0 && (yr % 100 != 0 || yr % 400 == 0))
    daysInMonth[1] = 29;

  DateTime first(now.year(), now.month(), 1);
  int col = first.dayOfTheWeek(); // 0=Ahad
  int row = 0;

  for (int d = 1; d <= daysInMonth[now.month() - 1]; d++) {
    int x = col * 18;
    int y = 24 + row * 7;

    if (d == now.day()) {
      display.fillRect(x - 1, y - 1, 16, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(d < 10 ? x + 6 : x + 2, y);
    display.print(d);
    display.setTextColor(SSD1306_WHITE);

    if (++col == 7) {
      col = 0;
      row++;
    }
  }
  display.display();
}

// ─── ROUTER UTAMA ────────────────────────────────────────

void runDisplay(DateTime now) {
  switch (activeLayout) {
  case LAYOUT_HOME_PRAYER:
    runOledHomePrayer(now);
    break;
  case LAYOUT_TAKWIM:
    runOledTakwim(now);
    break;
  default:
    runOledHomePrayer(now);
    break;
  }
}

#endif // DISPLAYMODULE_H