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
