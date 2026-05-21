#ifndef AUDIO_STORAGE_MODULE_H
#define AUDIO_STORAGE_MODULE_H

// Storan audio luaran: microSD (FAT) diutamakan; W25Q128 — pengesanan sahaja
// (FAT/LittleFS atas raw flashperlukan Fasa E penuh).

#include "config.h"
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <SPIMemory.h>

enum AudioStorageKind : uint8_t {
  AUDIO_STORAGE_NONE = 0,
  AUDIO_STORAGE_SD_CARD,
  AUDIO_STORAGE_W25Q128_PENDING, // pin sedia ada — mount FAT/LittleFS belum (Fasa E)
};

static AudioStorageKind g_audioStorageKind = AUDIO_STORAGE_NONE;
static SPIClass        *g_sdSpi = nullptr;

// Cuba kesan W25Q128 pada CS FLASH (kongsi VSPI dengan SD). Tiada mount FS —
// penanda sahaja untuk Fasa E (glue LittleFS/FAT atas raw flash).
static inline void audioStorageProbeW25q128() {
  if (!g_sdSpi)
    return;
  SPIFlash flash(FLASH_CS, g_sdSpi);
  if (!flash.begin()) {
    Serial.println(F("Storan: W25Q — tiada respons pada CS flash"));
    return;
  }
  uint32_t jedec = flash.getJEDECID();
  uint32_t cap   = flash.getCapacity();
  Serial.printf(
      "Storan: W25Q dikesan JEDEC=0x%06lX, kap=%lu bait (cache FS belum — "
      "guna microSD)\n",
      (unsigned long)jedec, (unsigned long)cap);
}

static inline bool initAudioStorage() {
  g_audioStorageKind = AUDIO_STORAGE_NONE;

  if (!g_sdSpi)
    g_sdSpi = new SPIClass(VSPI);
  g_sdSpi->begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (SD.begin(SD_CS, *g_sdSpi, 25000000, "/ext_sd", 5, false)) {
    g_audioStorageKind = AUDIO_STORAGE_SD_CARD;
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("Storan: SD kad OK — ~%s MB, mount /ext_sd\n",
                  String((unsigned long)cardSize).c_str());
    return true;
  }
  Serial.println(F("Storan: SD kad tidak dikesan atau gagal mount"));

  audioStorageProbeW25q128();
  Serial.println(F(
      "Storan: W25Q128 — tiada mount FS untuk cache (guna microSD untuk Tier 4)"));

  g_audioStorageKind = AUDIO_STORAGE_W25Q128_PENDING;

  return false;
}

static inline bool externalAudioReady() {
  return g_audioStorageKind == AUDIO_STORAGE_SD_CARD;
}

static inline AudioStorageKind audioStorageKind() { return g_audioStorageKind; }

static inline fs::FS *externalAudioFs() {
  return externalAudioReady() ? &SD : nullptr;
}

// Saiz bebas (0 jika tidak SD)
static inline uint64_t externalAudioFreeBytes() {
  if (!externalAudioReady())
    return 0;
  // Kad SD SPI: API totalBytes/usedBytes tidak seragam merentasi core
  return SD.cardSize();
}

#endif
