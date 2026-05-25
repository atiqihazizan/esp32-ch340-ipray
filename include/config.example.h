#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Salin fail ini sebagai config.h dan isikan SSID / kata laluan WiFi anda:
//   cp include/config.example.h include/config.h

// WiFi
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

// I2S Pins (MAX98357A)
#define I2S_LRC   26
#define I2S_BCLK  27
#define I2S_DIN   25

// ── W25Q128 SPI PINS (VSPI ESP32) ──
#define FLASH_CS    5
#define FLASH_CLK   18
#define FLASH_MISO  19
#define FLASH_MOSI  23

// ── microSD (SPI) — berkongsi bus VSPI; CS berbeza dari FLASH_CS ──
#define SD_CS       4
#define SD_SCK      FLASH_CLK
#define SD_MISO     FLASH_MISO
#define SD_MOSI     FLASH_MOSI

// W25Q128 belum dipasang → 0 (elak flash.begin() hang pada boot).
// Set 1 selepas chip dipasang dan wiring CS/SPI disahkan.
#ifndef AUDIO_STORAGE_PROBE_W25Q
#define AUDIO_STORAGE_PROBE_W25Q 0
#endif

// OLED (I2C)
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT   64
#define OLED_RESET      -1
#define SCREEN_ADDRESS  0x3C
#define OLED_SDA        21
#define OLED_SCL        22

// NTP & TTS
#define GMT_OFFSET (8 * 3600)
#define TTS_LANG   "en"
// #define TTS_LANG   "ms"

// ── VOLUME ──
// audioI2S range = 0..21
#define MAX_VOL    21

#endif
