#ifndef DISPLAY_MODULE_H
#define DISPLAY_MODULE_H

#include "config.h"
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include "data/PrayerData.h"
#include "data/ConfigModule.h"   
#include "SlideAnimUtil.h"

extern Adafruit_SSD1306 display;
extern String audioStatus;

// ================================================================
// ENUM LAYOUT — Tukar nilai ini untuk switch layout
// ================================================================
enum ScreenLayout {
  LAYOUT_HOME_STANDARD,  // Jam + Tarikh + status bar (WiFi/Audio)
  LAYOUT_HOME_PRAYER,    // Jam + Tarikh + waktu solat seterusnya
  LAYOUT_HOME_FLIPFLOP,  // Jam + Tarikh bertukar Gregorian ↔ Hijri
  LAYOUT_HOME_SLIDE,     // Jam + Saat gelongsor masuk dari bawah
  LAYOUT_TAKWIM,         // Kalendar bulanan
  // LAYOUT_FUTURE_X     // Ruang layout masa hadapan...
};

ScreenLayout activeLayout = LAYOUT_HOME_SLIDE; // Lalai; boleh ditimpa /config/display.json

inline bool setActiveLayoutByIndex(int idx) {
  if (idx < (int)LAYOUT_HOME_STANDARD || idx > (int)LAYOUT_TAKWIM)
    return false;
  activeLayout = static_cast<ScreenLayout>(idx);
  return true;
}

// ================================================================
// STRUCTS
// ================================================================
struct HijriDate { int day, month, year; };
// PrayerTime → digantikan oleh PrayerSlot dalam PrayerData.h

// ================================================================
// WAKTU SOLAT — Data diambil dari PrayerData.h
// ================================================================
const char* getNextPrayer(int h, int m, int &outH, int &outM) {
  int nowMin = h * 60 + m;
  for (int i = 0; i < PRAYER_COUNT; i++) {
    int pt = prayers[i].hour * 60 + prayers[i].minute;
    if (pt > nowMin) {
      outH = prayers[i].hour;
      outM = prayers[i].minute;
      return prayers[i].name;
    }
  }
  // Lepas Isyak → Subuh esok
  outH = prayers[0].hour;
  outM = prayers[0].minute;
  return prayers[0].name;
}

// ================================================================
// KONVERSI GREGORIAN → HIJRI
// ================================================================
HijriDate toHijri(int gd, int gm, int gy) {
  int a = (14 - gm) / 12, y = gy + 4800 - a, m = gm + 12 * a - 3;
  long jdn = (long)gd + (153L * m + 2) / 5 + 365L * y
             + y / 4 - y / 100 + y / 400 - 32045;
  long l = jdn - 1948440L + 10632;
  long n = (l - 1) / 10631;
  l = l - 10631L * n + 354;
  long j = ((10985 - l) / 5316) * ((50L * l) / 17719)
           + (l / 5670) * ((43L * l) / 15238);
  l = l - ((30 - j) / 15) * ((17719L * j) / 50)
        - (j / 16) * ((15238L * j) / 43) + 29;
  HijriDate h;
  h.month = (int)((24L * l) / 709);
  h.day   = (int)(l - (709L * h.month) / 24);
  h.year  = (int)(30 * n + j - 30);
  return h;
}

const char* hijriMonthShort(int m) {
  const char* n[] = {
    "Mhrm","Safar","R.Awl","R.Akh",
    "J.Awl","J.Akh","Rejab","Sybn",
    "Rmdn","Sywal","Z.Qed","Z.Hjj"
  };
  return (m >= 1 && m <= 12) ? n[m - 1] : "?";
}

// ================================================================
// INIT
// ================================================================
void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    Serial.println(F("SSD1306 failed"));
  display.clearDisplay();
  display.display();
}

// Status bar utama: papar IP STA atau IP hotspot setup (max ~11 aksara kerana lebar OLED)
inline void printOledWifiIpStatus() {
  String s;
  if (WiFi.status() == WL_CONNECTED)
    s = WiFi.localIP().toString();
  else {
    wifi_mode_t m = WiFi.getMode();
    if (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA)
      s = WiFi.softAPIP().toString();
    else
      s = "WiFi:--";
  }
  
  // const int maxCh = 11;
  // if ((int)s.length() > maxCh)
  //   s = s.substring((unsigned)((int)s.length() - maxCh));
  display.print(s);
}

// ================================================================
// KOMPONEN BOOT (Spinner & Progress Bar)
// ================================================================
static int  spinner_idx   = 0;
static const char spinner_chars[] = {'|', '/', '-', '\\'}; // static — elak multiple definition

void drawSpinner(int x, int y) {
  static uint32_t lastSpin = 0;
  if (millis() - lastSpin > 150) {
    spinner_idx = (spinner_idx + 1) % 4;
    lastSpin = millis();
  }
  display.setCursor(x, y);
  display.print(spinner_chars[spinner_idx]);
}

void drawProgressBar(int progress) {
  display.drawRect(0, 45, 128, 10, SSD1306_WHITE);
  int fill = (progress * (128 - 4)) / 100;
  display.fillRect(2, 47, fill, 6, SSD1306_WHITE);
}

// ================================================================
// BOOT: Splash Logo
// ================================================================
void showSplashLogo(int durationMs = 2000) {
  const int cx = 64, cy = 26;
  display.clearDisplay();

  // Ikon jam berlapis
  display.drawCircle(cx, cy, 18, SSD1306_WHITE);
  display.drawCircle(cx, cy, 12, SSD1306_WHITE);
  display.fillCircle(cx, cy,  4, SSD1306_WHITE);
  display.drawLine(cx, cy, cx,     cy - 10, SSD1306_WHITE); // jarum 12
  display.drawLine(cx, cy, cx + 8, cy,      SSD1306_WHITE); // jarum 3

  // Nama sistem
  const char* title = "MY DEVICE";
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.drawLine(0, 50, 128, 50, SSD1306_WHITE);
  display.setCursor(64 - (int)(strlen(title) * 6) / 2, 55);
  display.print(title);

  display.display();
  delay(durationMs);
}

// ================================================================
// BOOT: Status Booting (dengan spinner)
// ================================================================
void showBootStatus(String msg, int progress) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 5);
  display.println("SYSTEM INITIALIZING");
  display.drawLine(0, 15, 128, 15, SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print("> " + msg);
  drawSpinner(115, 25);
  drawProgressBar(progress);
  display.display();
}

// ================================================================
// BOOT: RTC Reset + Tiada Internet (Masa Tidak Sah)
// ================================================================
//
//  ┌────────────────────────┐
//  │████ ! MASA TIDAK SAH ! █│  ← Header inverted
//  │                        │
//  │ RTC telah reset.       │
//  │ Tiada WiFi untuk sync. │
//  │────────────────────────│
//  │ Masa mungkin tidak     │
//  │ tepat sehingga sync.   │
//  └────────────────────────┘
//
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

// ================================================================
// BOOT: WiFi Gagal
// ================================================================
//
//  ┌────────────────────────┐
//  │█████ ! WIFI GAGAL ! ████│  ← Header inverted
//  │                        │
//  │ Tiada sambungan WiFi.  │
//  │ TTS tidak tersedia.    │
//  │────────────────────────│
//  │ > Mod Offline aktif.   │
//  │ > Jam tetap berjalan.  │
//  └────────────────────────┘
//
void showWiFiError() {
  display.clearDisplay();
  display.setTextSize(1);

  // Header inverted
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(20, 3);
  display.print("! WIFI FAILED !");

  // Body
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 15);
  display.print("No WiFi connection.");
  display.setCursor(0, 25);
  display.print("TTS unavailable.");

  // Divider
  display.drawLine(0, 36, 128, 36, SSD1306_WHITE);

  // Status offline
  display.setCursor(0, 40);
  display.print("> Offline mode.");
  display.setCursor(0, 51);
  display.print("> Clock still runs.");

  display.display();
}

// ================================================================
// LAYOUT HOME 1: STANDARD
// ================================================================
//
//  ┌────────────────────────┐
//  │                        │
//  │      12:34:56          │  ← textSize 2
//  │                        │
//  │      08/05/2026        │  ← textSize 1
//  │                        │
//  │────────────────────────│
//  │ WIFI:OK       IDLE     │
//  └────────────────────────┘
//
void runOledHomeStandard(DateTime now) {
  static SlideField fHour, fMin, fSec;
  static SlideField fDay, fMonth, fYear;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── JAM (textSize 2) — di tengah, sama macam original (cursor x=16, y=8) ──
  const int CLOCK_Y = 8;
  const int X_HH    = 16;
  const int X_C1    = X_HH + 24;     // x=40
  const int X_MM    = X_C1 + 12;     // x=52
  const int X_C2    = X_MM + 24;     // x=76
  const int X_SS    = X_C2 + 12;     // x=88

  slideDrawField(fHour, now.hour(),   X_HH, CLOCK_Y, 2, 2);
  slideDrawField(fMin,  now.minute(), X_MM, CLOCK_Y, 2, 2);
  slideDrawField(fSec,  now.second(), X_SS, CLOCK_Y, 2, 2);

  slideDrawSeparator(":", X_C1, CLOCK_Y, 2);
  slideDrawSeparator(":", X_C2, CLOCK_Y, 2);

  // ── TARIKH (textSize 1) — di tengah, original x=29, y=30 ──
  const int DATE_Y = 30;
  const int X_DD   = 29;
  const int X_S1   = X_DD + 12;       // x=41
  const int X_MO   = X_S1 + 6;        // x=47
  const int X_S2   = X_MO + 12;       // x=59
  const int X_YR   = X_S2 + 6;        // x=65

  slideDrawField(fDay,   now.day(),   X_DD, DATE_Y, 1, 2);
  slideDrawField(fMonth, now.month(), X_MO, DATE_Y, 1, 2);
  slideDrawField(fYear,  now.year(),  X_YR, DATE_Y, 1, 4);

  slideDrawSeparator("/", X_S1, DATE_Y, 1);
  slideDrawSeparator("/", X_S2, DATE_Y, 1);

  // ── Status bar ──
  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 57);
  printOledWifiIpStatus();
  display.setCursor(85, 57);
  display.print(audioStatus);

  display.display();
}

// ================================================================
// LAYOUT HOME 2: + WAKTU SOLAT (3-panel flipflop bawah)
// ================================================================
//
//  Panel A (Next Prayer):    Panel B (All Prayers):     Panel C (Network):
//  ┌────────────────────┐    ┌────────────────────┐    ┌────────────────────┐
//  │     12:34:56       │    │     12:34:56       │    │     12:34:56       │
//  │     08/05/2026     │    │     08/05/2026     │    │     08/05/2026     │
//  │ ───────────────── │    │ ─────────────────  │    │ ─────────────────  │
//  │ Solat: Asar 16:30 │    │ Sub 05:51 Zhr 13:12│    │ IP: 192.168.1.50  │
//  │ Masa : 1j 55m lagi│    │[Asr 16:33]Mgb 19:20│    │ Host: my-clock     │
//  │                    │    │ Isy 20:32 Syr 07:01│    │                    │
//  └────────────────────┘    └────────────────────┘    └────────────────────┘
//
//  Bottom panel flipflop tiap 4 saat (independent dari date flipflop).
//
void runOledHomePrayer(DateTime now) {
  static SlideField fHour, fMin, fSec;

  // Date flipflop (Masihi ↔ Hijri)
  static bool showHijri = false;
  static uint32_t lastShowHijri = 0;
  if (millis() - lastShowHijri > 4000) {
    showHijri = !showHijri;
    lastShowHijri = millis();
  }

  // Panel flipflop (A → B → C → A → ...)
  static int      panelIdx     = 0;     // 0=NextPrayer, 1=AllPrayers, 2=Network
  static uint32_t lastPanelFlip = 0;
  if (millis() - lastPanelFlip > 4000) {
    panelIdx = (panelIdx + 1) % 3;
    lastPanelFlip = millis();
  }

  // Cache hostname (slow SPIFFS read — ambil sekali sahaja)
  static char cachedHostname[33] = {0};
  static bool hostnameInit = false;
  if (!hostnameInit) {
    ConfigWiFi w = getWiFiConfig();
    strncpy(cachedHostname, w.hostname, 32);
    cachedHostname[32] = '\0';
    if (cachedHostname[0] == '\0')
      strncpy(cachedHostname, "my-clock", 32);
    hostnameInit = true;
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── JAM (textSize 2) — animated ──
  const int CLOCK_Y = 4;
  const int X_HH    = 16;
  const int X_C1    = X_HH + 24;
  const int X_MM    = X_C1 + 12;
  const int X_C2    = X_MM + 24;
  const int X_SS    = X_C2 + 12;

  slideDrawField(fHour, now.hour(),   X_HH, CLOCK_Y, 2, 2);
  slideDrawField(fMin,  now.minute(), X_MM, CLOCK_Y, 2, 2);
  slideDrawField(fSec,  now.second(), X_SS, CLOCK_Y, 2, 2);
  slideDrawSeparator(":", X_C1, CLOCK_Y, 2);
  slideDrawSeparator(":", X_C2, CLOCK_Y, 2);

  // ── TARIKH (Masihi/Hijri flipflop) ──
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

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

  // ── Garis pemisah ──
  display.drawLine(0, 34, 128, 34, SSD1306_WHITE);

  // ── BOTTOM PANEL — FLIPFLOP ──
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (panelIdx == 0) {
    // ============ PANEL A: NEXT PRAYER ============
    int nextH, nextM;
    const char *pName = getNextPrayer(now.hour(), now.minute(), nextH, nextM);
    int diff = (nextH * 60 + nextM) - (now.hour() * 60 + now.minute());
    if (diff < 0) diff += 1440;

    display.setCursor(0, 38);
    display.printf("Solat : %-6s %02d:%02d", pName, nextH, nextM);
    display.setCursor(0, 50);
    display.printf("Masa  : %dj %02dm lagi", diff / 60, diff % 60);

  } else if (panelIdx == 1) {
    // ============ PANEL B: SEMUA PRAYERS + HIGHLIGHT NEXT ============
    // 6 waktu, 2 column × 3 row
    // Order yang sesuai untuk paparan: Sub, Zhr, Asr, Mgb, Isy, Syr
    // (Subuh dulu, Syuruk last sebab dia "tambahan" bukan waktu solat fardu)

    // Cari index next prayer dalam prayers[] (untuk highlight)
    int nowMin   = now.hour() * 60 + now.minute();
    int nextIdx  = -1;
    for (int i = 0; i < PRAYER_COUNT; i++) {
      int pt = prayers[i].hour * 60 + prayers[i].minute;
      if (pt > nowMin) {
        nextIdx = i;
        break;
      }
    }
    if (nextIdx == -1) nextIdx = 0;  // lepas Isyak → next = Subuh esok

    // Susunan paparan: index dalam prayers[] dan posisi grid (col, row)
    // prayers[]: 0=Subuh, 1=Syuruk, 2=Zohor, 3=Asar, 4=Maghrib, 5=Isyak
    struct PrayerCell {
      int prayerIdx;
      int col;  // 0 atau 1
      int row;  // 0, 1, 2
    };
    static const PrayerCell layout[6] = {
      { 0, 0, 0 },  // Subuh    → kiri atas
      { 2, 1, 0 },  // Zohor    → kanan atas
      { 3, 0, 1 },  // Asar     → kiri tengah
      { 4, 1, 1 },  // Maghrib  → kanan tengah
      { 5, 0, 2 },  // Isyak    → kiri bawah
      { 1, 1, 2 },  // Syuruk   → kanan bawah
    };

    const int CELL_W = 64;   // 128 / 2
    const int CELL_H = 9;    // 27 / 3
    const int Y_BASE = 37;

    for (int i = 0; i < 6; i++) {
      const PrayerCell &c = layout[i];
      int pi = c.prayerIdx;

      int x = c.col * CELL_W;
      int y = Y_BASE + c.row * CELL_H;

      bool isNext = (pi == nextIdx);

      if (isNext) {
        // Inverted highlight
        display.fillRect(x, y - 1, CELL_W, CELL_H, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      } else {
        display.setTextColor(SSD1306_WHITE);
      }

      // Format pendek: "Sub 5:51" (3-char name + spasi + H:MM)
      // Sebab cell 64px ÷ 6px/char = 10 char max
      char buf[16];
      // prayers[i].name boleh 4-6 char ("Subuh", "Zohor", "Mgrb")
      // Guna 3 char abbrev untuk pastikan muat
      const char *full = prayers[pi].name;
      char shortName[4];
      shortName[0] = full[0];
      shortName[1] = full[1] ? full[1] : ' ';
      shortName[2] = full[2] ? full[2] : ' ';
      shortName[3] = '\0';

      snprintf(buf, sizeof(buf), "%s %d:%02d", shortName,
               prayers[pi].hour, prayers[pi].minute);

      display.setCursor(x + 1, y);
      display.print(buf);
    }
    display.setTextColor(SSD1306_WHITE);  // reset

  } else {
    // ============ PANEL C: NETWORK INFO ============
    display.setCursor(0, 38);
    display.print("IP  : ");
    if (WiFi.status() == WL_CONNECTED) {
      display.print(WiFi.localIP().toString());
    } else {
      wifi_mode_t m = WiFi.getMode();
      if (m == WIFI_MODE_AP || m == WIFI_MODE_APSTA)
        display.print(WiFi.softAPIP().toString());
      else
        display.print("--");
    }

    display.setCursor(0, 50);
    display.printf("Host: %s", cachedHostname);
  }

  display.display();
}

// ================================================================
// LAYOUT HOME 3: FLIPFLOP GREGORIAN ↔ HIJRI
// ================================================================
//
//  Mode [M] Masihi:           Mode [H] Hijri:
//  ┌────────────────────┐     ┌────────────────────┐
//  │    12:34:56        │     │    12:34:56         │
//  │    08/05/2026      │     │    11 Z.Hjj 1447H   │
//  │       [M]          │     │         [H]         │
//  │────────────────────│     │─────────────────────│
//  │ WIFI:OK     IDLE   │     │ WIFI:OK      IDLE   │
//  └────────────────────┘     └─────────────────────┘
//
void runOledHomeFlipFlop(DateTime now) {
  static SlideField fHour, fMin, fSec;
  static SlideField fDay, fMonth, fYear;
  static bool showHijri = false;
  static uint32_t lastFlip = 0;

  if (millis() - lastFlip > 4000) { // Tukar setiap 4 saat
    showHijri = !showHijri;
    lastFlip = millis();
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── JAM (textSize 2) — original x=16, y=8 ──
  const int CLOCK_Y = 8;
  const int X_HH    = 16;
  const int X_C1    = X_HH + 24;     // x=40
  const int X_MM    = X_C1 + 12;     // x=52
  const int X_C2    = X_MM + 24;     // x=76
  const int X_SS    = X_C2 + 12;     // x=88

  slideDrawField(fHour, now.hour(),   X_HH, CLOCK_Y, 2, 2);
  slideDrawField(fMin,  now.minute(), X_MM, CLOCK_Y, 2, 2);
  slideDrawField(fSec,  now.second(), X_SS, CLOCK_Y, 2, 2);

  slideDrawSeparator(":", X_C1, CLOCK_Y, 2);
  slideDrawSeparator(":", X_C2, CLOCK_Y, 2);

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (!showHijri) {
    // ── TARIKH MASIHI — text static (tengahkan) ──
    char ds[11];
    sprintf(ds, "%02d/%02d/%04d", now.day(), now.month(), now.year());
    display.setCursor(64 - (int)(strlen(ds) * 3), 28); // tengahkan
    display.print(ds);
  } else {
    // ── TARIKH HIJRI — text static (tengahkan) ──
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
    display.setCursor(64 - (int)(strlen(hs) * 3), 28); // tengahkan
    display.print(hs);
  }

  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setCursor(2, 57);
  printOledWifiIpStatus();
  display.setCursor(85, 57);
  display.print(audioStatus);

  display.display();
}

// ================================================================
// LAYOUT HOME 4: SAAT GELONGSOR — SEMUA FIELD ANIMATED (FIXED)
// ================================================================
// PENTING: Urutan lukis kini:
//   1. clearDisplay
//   2. Lukis SEMUA field bernombor dulu (slideDrawField)
//   3. Lukis SEMUA separator selepas (atas mana-mana clip yg tertinggal)
//   4. Lukis garis & status bar
//
// Sebab: slideDrawField clip atas+bawah zonnya — kalau separator
// dilukis SEBELUM field jiran, separator akan dipadam oleh clip.
// ================================================================
void runOledHomeSlide(DateTime now) {
  static SlideField fHour, fMin, fSec;
  static SlideField fDay, fMonth, fYear;

  display.clearDisplay();

  // ── POSISI JAM (textSize 2) ──
  // Beri spacing extra antara field untuk elak clip tindih
  // HH(24px) + gap(4px) + :(12px) + gap(4px) + MM(24px) + ...
  // Sebenarnya separator letak antara field tanpa gap — clip-zone
  // field berbeza dengan zon separator, jadi tak tindih.
  const int CLOCK_Y = 8;
  const int X_HH    = 16;            // jam mula di x=16
  const int X_C1    = X_HH + 24;     // x=40, ":" pertama
  const int X_MM    = X_C1 + 12;     // x=52
  const int X_C2    = X_MM + 24;     // x=76, ":" kedua
  const int X_SS    = X_C2 + 12;     // x=88

  // 1. Lukis FIELD bernombor dulu
  slideDrawField(fHour, now.hour(),   X_HH, CLOCK_Y, 2, 2);
  slideDrawField(fMin,  now.minute(), X_MM, CLOCK_Y, 2, 2);
  slideDrawField(fSec,  now.second(), X_SS, CLOCK_Y, 2, 2);

  // 2. Lukis SEPARATOR selepas (zon tak tindih dengan field)
  slideDrawSeparator(":", X_C1, CLOCK_Y, 2);
  slideDrawSeparator(":", X_C2, CLOCK_Y, 2);

  // ── POSISI TARIKH (textSize 1) ──
  const int DATE_Y = 30;
  const int X_DD   = 34;
  const int X_S1   = X_DD + 12;       // x=46
  const int X_MO   = X_S1 + 6;        // x=52
  const int X_S2   = X_MO + 12;       // x=64
  const int X_YR   = X_S2 + 6;        // x=70

  slideDrawField(fDay,   now.day(),   X_DD, DATE_Y, 1, 2);
  slideDrawField(fMonth, now.month(), X_MO, DATE_Y, 1, 2);
  slideDrawField(fYear,  now.year(),  X_YR, DATE_Y, 1, 4);

  slideDrawSeparator("/", X_S1, DATE_Y, 1);
  slideDrawSeparator("/", X_S2, DATE_Y, 1);

  // ── Status bar (statik) ──
  display.drawLine(0, 54, 128, 54, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 57);
  display.print(WiFi.status() == WL_CONNECTED ? "WIFI:OK" : "WIFI:ER");
  display.setCursor(85, 57);
  display.print(audioStatus);

  display.display();
}

// ================================================================
// LAYOUT: TAKWIM (Kalendar Bulanan)
// ================================================================
//
//  ┌────────────────────────┐
//  │█████ Mei 2026 ██████████│  ← Header inverted
//  │ Ah Is Se Ra Kh Ju Sa   │  ← Nama hari
//  │─────────────────────── │
//  │          1  2  3  4  5 │
//  │  6  7  8  9 10 11 12   │
//  │ 13 14 15[16]17 18 19   │  ← [16] = hari ini, highlight
//  │ 20 21 22 23 24 25 26   │
//  │ 27 28 29 30 31         │
//  └────────────────────────┘
//
void runOledTakwim(DateTime now) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // ── Header (inverted) ──
  display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);

  const char* mNames[] = {
    "Januari","Februari","Mac","April","Mei","Jun",
    "Julai","Ogos","September","Oktober","November","Disember"
  };
  char title[18];
  sprintf(title, "%s %d", mNames[now.month() - 1], now.year());
  display.setCursor(64 - (int)(strlen(title) * 3), 3);
  display.print(title);

  // ── Nama hari ──
  display.setTextColor(SSD1306_WHITE);
  const char* dh[] = {"Ah","Is","Se","Ra","Kh","Ju","Sa"};
  for (int i = 0; i < 7; i++) {
    display.setCursor(i * 18 + 3, 14);
    display.print(dh[i]);
  }
  display.drawLine(0, 22, 128, 22, SSD1306_WHITE);

  // ── Grid tarikh ──
  int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int yr = now.year();
  if ((yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0)
    daysInMonth[1] = 29;

  DateTime first(now.year(), now.month(), 1);
  int col = first.dayOfTheWeek(); // 0=Ahad
  int row = 0;

  for (int d = 1; d <= daysInMonth[now.month() - 1]; d++) {
    int x = col * 18;
    int y = 24 + row * 7;

    if (d == now.day()) {
      // Highlight hari ini (inverted box)
      display.fillRect(x + 1, y - 1, 16, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(d < 10 ? x + 6 : x + 2, y);
    display.print(d);
    display.setTextColor(SSD1306_WHITE);

    if (++col >= 7) { col = 0; row++; }
  }

  display.display();
}

// ================================================================
// ROUTER UTAMA — Panggil ini dalam loop()
// ================================================================
void runDisplay(DateTime now) {
  switch (activeLayout) {
    case LAYOUT_HOME_STANDARD: runOledHomeStandard(now); break;
    case LAYOUT_HOME_PRAYER:   runOledHomePrayer(now);   break;
    case LAYOUT_HOME_FLIPFLOP: runOledHomeFlipFlop(now); break;
    case LAYOUT_HOME_SLIDE:    runOledHomeSlide(now);    break;
    case LAYOUT_TAKWIM:        runOledTakwim(now);       break;
  }
}

#endif