# Kesimpulan pembetulan firmware (2026-05-25)

Dokumen ini untuk konteks Claude / pembangunan seterusnya.

## Status hardware semasa

- **W25Q128 belum dipasang** (sengaja — probe SPI pernah tidak stabil / hang).
- **microSD** tiada atau gagal mount — normal; log `[E] sd_diskio` boleh diabaikan.
- **RTC DS3231**, **OLED I2C**, **WiFi STA**, **takwim SPIFFS**, **web :80** — disahkan OK pada boot terakhir.

## Boot hang — punca sebenar (disahkan serial)

Log boot berhenti selepas `Storan: SD kad tidak dikesan...` tanpa baris `RTC:`.

| Punca | Penjelasan |
|--------|------------|
| **Utama (disahkan)** | Selepas SD gagal, `initAudioStorage()` memanggil `audioStorageProbeW25q128()` → `SPIFlash::begin()` pada CS flash **tanpa chip**. Transaksi SPI terapung → **hang** sebelum `initDisplay()` / `initTime()`. |
| **Sekunder (tetap wajib)** | `initDisplay()` tidak dipanggil dalam `setup()` pada satu ketika — `Wire.begin()` untuk I2C mesti ada **sebelum** `rtc.begin(&Wire)`. Tanpa itu, RTC juga boleh hang; tetapi pada board pengguna, hang pertama kali adalah probe W25Q. |

**Bukti boot OK:** selepas probe W25Q dilangkau, urutan serial:

```
Storan: W25Q — probe dilangkau (...)
RTC: OK
mDNS / WiFi STA / NTP dilangkau / Audio / Takwim / Web :80
```

## Perubahan kod yang dilaksanakan

### Keutamaan 0c — `platformio.ini` (stack `loopTask`)

- `build_flags`: `-DARDUINO_LOOP_STACK_SIZE=16384` — elak `stack overflow in task loopTask` selepas boot lengkap.
- Sandaran: `backup/2026-05-25/platformio.ini`.

### Keutamaan 0b — `src/main.cpp`

- `initDisplay()` **sebelum** `initTime()` (selepas `initAudioStorage()`).

### Boot hang W25Q — `include/core/AudioStorageModule.h`

- Lalai `AUDIO_STORAGE_PROBE_W25Q` = **0** (juga dalam `config.example.h`).
- Bila 0: tiada `flash.begin()`, tiada include SPIMemory dalam build path probe.
- Bila chip dipasang nanti: set `#define AUDIO_STORAGE_PROBE_W25Q 1` dalam `config.h`.

### Keutamaan 1–5 (sedia ada / disahkan)

| # | Fail | Ringkas |
|---|------|---------|
| 1–2 | `AudioModule.h` | `audioStopRequested`; `stopAndFlushAudio()` tidak panggil `audio.stopSong()` luar task; `AudioLoopTask` sahaja sentuh `audio`; tiada WiFi → `/b3.wav` sandaran |
| 3 | `WebServer.h` | `clockWebPathSensitif` — GET/PUT `/config/wifi.json` → 403 |
| 4 | `AudioModule.h` | Log jika `ttsQueue` penuh |
| 5 | `main.cpp` | `Serial.setTimeout(30)` |

### Tidak dilaksanakan (sengaja)

Autentikasi web, mutex SPIFFS penuh, retry STA, mDNS unik, LittleFS, ubah `warn_after`, brown-out disable (`WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)`) — tanya pengguna jika mahu pulihkan.

## Arahan untuk Claude seterusnya

1. Jangan hidupkan probe W25Q (`AUDIO_STORAGE_PROBE_W25Q=1`) sehingga pengguna sahkan chip dipasang dan wiring VSPI/CS stabil.
2. Kekalkan urutan boot: `initConfigStorage` → `initAudioStorage` → `initDisplay` → `initTime` → splash/WiFi → audio task → beep/takwim/web.
3. Log SD error tanpa kad adalah **bukan** kegagalan boot.
4. `gemini.key` jangan commit.

## Ujian pantas selepas ubah firmware

```bash
pio run -t upload && pio device monitor
```

Cari: baris probe dilangkau → `RTC: OK` → WiFi IP → `Web: pelayan HTTP :80` → **tiada** `stack overflow in task loopTask`; OLED kekal, tiada reboot berulang.
