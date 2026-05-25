# ESP32 Smart Clock - Tier 3 Implementation

## Project Context

Smart prayer time clock with OLED display, Google TTS announcements, and beep alerts.
Currently Tier 2 complete (takwim from SPIFFS works). Now implementing Tier 3 (web configuration).

## Hardware

- **MCU**: ESP32 dev board (4MB flash, no PSRAM, ESP32-D0WD-V3)
- **RTC**: DS3231 via I2C (default pins)
- **Display**: SSD1306 OLED 128x64 via I2C (address 0x3C)
- **Audio**: MAX98357A I2S amplifier
  - BCLK = GPIO 27
  - LRC = GPIO 26
  - DIN = GPIO 25
- **Build**: PlatformIO with `huge_app.csv` partition (3MB code, 0.9MB SPIFFS)

## Build & Test Commands

```bash
pio run                  # Compile only (verify before upload)
pio run -t upload        # Flash firmware
pio run -t uploadfs      # Flash SPIFFS data folder
pio device monitor       # Open serial @ 115200
```

**IMPORTANT**: After uploading, monitor serial for 30+ seconds to verify boot sequence.

## Code Style Conventions

- **Header-only modules**: All logic in `.h` files (definitions inline)
- **Language for comments**: Bahasa Melayu (Malay)
- **Naming**: 
  - Functions: `camelCase` 
  - Types/Structs: `PascalCase`
  - Constants/Macros: `UPPER_SNAKE_CASE`
- **Style**: Simple, hobby project — avoid over-engineering

## Folder Structure (selepas Fasa 1 — semasa)

include/
├── core/
│   ├── TimeModule.h
│   ├── AudioModule.h
│   ├── DisplayModule.h
│   └── SlideAnimUtil.h       (utility animasi paparan)
├── data/
│   ├── ConfigModule.h        (JSON SPIFFS — Fasa 2)
│   ├── TakwimModule.h
│   └── PrayerData.h
├── network/
│   ├── WiFiManager.h         (Fasa 3 — STA / AP / mDNS)
│   ├── WebServer.h           (Fasa 4–6; guard `MY_CLOCK_WEB_SERVER_H`; guna `<WebServer.h>` framework)
│   ├── HTTPDownload.h        (Fasa 5 — HTTPClient → SPIFFS /takwim.txt)
│   └── NTPManager.h          (Fasa 6 — pencetus NTP manual)
├── logic/
│   ├── AnnounceModule.h
│   └── BeepModule.h
├── config.h                   (compile-time defaults; jangan commit kredensial)
├── config.example.h
└── helpers.h                  (deprecated, kosong)
src/
└── main.cpp
data/
├── takwim.txt
└── web/
    └── index.html             (muat naik SPIFFS: `pio run -t uploadfs`)

## Tier 3 Implementation Plan

### Phase 1: Folder Reorganization — SELESAI

Struktur seperti rajah di atas; lintasan `#include` dari `include/` (contoh `core/...`, `data/...`, `logic/...`, `network/...`).

### Phase 2: ConfigModule (JSON-based) — SELESAI

- Library: ArduinoJson **v7** (`JsonDocument doc;` heap — tiada ctor saiz seperti v6)
- Storage: SPIFFS `/config/` — fail boleh dicipta lepas POST web (Fasa 4); first boot fallback `config.h` + sandaran kod (hostname `my-clock`, zon takwim lalai `PNG01`).
- Fail & kunci JSON: sama seperti rajah Tier 3; ayat `tts_lang` (bawah garis dalam JSON).

API utama: `getWiFiConfig` / `saveWiFiConfig`, sama untuk Takwim, Display, Audio, Announce; `initConfigStorage()` awal dalam `setup()`.

Integrasi masa boot: WiFi daripada `wifi.json`; `applyAudioRuntimeConfig` + `activeTtsLang`; `setActiveLayoutByIndex`; `applyAnnounceRuntimeConfig`.

**ArduinoJson v7**: jangan gunakan `JsonDocument(n)` — guna `JsonDocument doc;` sahaja.

### Phase 3: WiFiManager (Strategy A) — SELESAI

- Boot: STA dengan kredensial `getWiFiConfig()` (fail `/config/wifi.json` atau lalai `config.h`).
- Masa tunggu STA: **15 s** dengan `showBootStatus` (progress seperti sebelum ini).
- Jika STA gagal: lepaskan sambungan, `WiFi.mode(WIFI_AP)`, `softAP` **MyClock-Setup** / **12345678**, IP lalai **192.168.4.1** (`softAPConfig`).
- **mDNS**: `MDNS.begin(hostname)` dengan hostname dari JSON (kosong → `my-clock`); sama untuk STA dan AP.
- **OLED**: `printOledWifiIpStatus()` pada bar status layout Standard, FlipFlop & Slide — papar IP STA atau IP AP, pendekkan jika panjang.
- Bantuan: `wifiIsStaLinked()`, `wifiIsSoftApMode()` dalam `WiFiManager.h` (untuk Fasa 4).

### Phase 4: Web Server (Plain HTML) — SELESAI

- `WebServer` framework port **80** (STA atau AP); `clockWebServerBegin()` / `clockWebServerLoop()` dalam `main`.
- HTML: `data/web/index.html` — **mesti** `pio run -t uploadfs` supaya `/web/index.html` wujud di SPIFFS.
- POST body: `application/x-www-form-urlencoded` medan **`data=`** (JSON string) — serasi borang web.
- Endpoint tambahan: **`POST /api/config/takwim`** — simpan URL/zon tanpa muat turun.

### Phase 5: Takwim URL Download — SELESAI

- `HTTPDownload.h`: `downloadTakwimFromConfig()` — stream HTTP → fail sementara → sahkan format → ganti `/takwim.txt`.
- URL: ganti **`%ZONE%`**; jika zon tidak kosong & URL tiada `zon=`, tambah `?zon=` atau `&zon=`.
- **Tiada** API penjejakan senarai zon penuh (pengguna isi URL + zon manual); semakan “online” = `WiFi.status() == WL_CONNECTED`.

### Phase 6: Manual NTP & Layout Switching — SELESAI

- **`POST /api/action/ntp_sync`** → `clockNtpManualSync()` → `syncNTP()` (perlukan STA).
- **`POST /api/config/display`** → paparan terus ikut `/config/display.json` (ArduinoJson simpan).

**Global reboot:** pembolehubah `clockWebRebootSoon` (definisi `main.cpp`) — selepas **`POST`** Wi-Fi / reboot, `ESP.restart()` selepas beberapa kitaran loop.

## Boot & storan luaran (2026-05-25 — disahkan hardware)

- **Hang boot** pernah berlaku selepas mesej SD gagal: punca utama **`flash.begin()` probe W25Q** bila chip **belum dipasang**. Lalai `AUDIO_STORAGE_PROBE_W25Q=0` dalam `AudioStorageModule.h` / `config.example.h`; hidupkan `1` dalam `config.h` hanya selepas W25Q dipasang.
- **`initDisplay()`** mesti dipanggil dalam `setup()` **sebelum** `initTime()` (`Wire.begin` untuk OLED + RTC).
- Boot OK ditanda serial: `Storan: W25Q — probe dilangkau` → `RTC: OK` → WiFi/mDNS → takwim → `Web: pelayan HTTP :80`.
- Rincian penuh: `history-development/2026-05-25-pembetulan-boot-dan-firmware.md`.

## Constraints

- **DO NOT** add Bluetooth/BLE (too large)
- **DO NOT** use OTA firmware update partition
- **DO NOT** break Tier 1+2 functionality (audio, beep, takwim, display must keep working)
- **DO** keep all existing layouts (5 layouts in DisplayModule)
- **DO** maintain existing module structure (header-only)

## Workflow Rules

1. Always run `pio run` after each significant change
2. Show me the plan BEFORE writing code, ask approval
3. After implementing each Phase, ask me to test on hardware
4. If compile fails, attempt 1 fix, then ask me
5. Don't modify hardware-specific code (pin assignments) without asking
6. When in doubt, ask — don't assume

## Testing Hardware (Manual Steps for Me)

Tell me to:
- "Cabut WiFi router, reboot ESP32, check OLED" — for AP fallback test
- "Phone connect to MyClock-Setup, open browser http://192.168.4.1" — for web UI test
- "Paste serial monitor output for boot sequence" — for verification

## Files I'm Sharing
- `main.cpp` and all `.h` files in `include/`
- `platformio.ini` 
- `data/takwim.txt`