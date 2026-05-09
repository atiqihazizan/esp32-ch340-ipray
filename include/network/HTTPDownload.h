#ifndef HTTP_DOWNLOAD_H
#define HTTP_DOWNLOAD_H

// Fasa 5: HTTPClient → muat turun takwim (stream ke SPIFFS)

#include "data/ConfigModule.h"
#include "data/TakwimModule.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WiFi.h>

// URL penuh: ganti %ZONE% atau %s (placeholder biasa API) dengan cfg.zone;
// Tambah zon= atau ?zon= **hanya** jika zon diisi tetapi URL langsung tidak
// mempunyai kunci zon/zone (elak salah baca "zon=" pada "zone=").
inline bool takwimResolveDownloadUrl(char *out, size_t n, const ConfigTakwim &cfg) {
  if (!out || n < 4)
    return false;
  out[0] = '\0';
  if (!cfg.url[0])
    return false;

  String u(cfg.url);

  const bool adaZonMasuk = cfg.zone[0] != '\0';
  bool     adaPlaceholder =
      u.indexOf("%ZONE%") >= 0 ||
      u.indexOf("%s") >= 0; // template seperti zone=%s

  u.replace("%ZONE%", adaZonMasuk ? String(cfg.zone) : String(""));
  // %s digunakan API kerajaan / contoh dokumentasi printf
  u.replace("%s", adaZonMasuk ? String(cfg.zone) : String(""));

  if (adaZonMasuk) {
    bool adaKunciZone =
        (u.indexOf("zone=") >= 0) || (u.indexOf("&zon=") >= 0) ||
        (u.indexOf("?zon=") >= 0); // beberapa endpoint guna zon= sahaja

    if (!adaKunciZone && !adaPlaceholder) {
      if (u.indexOf('?') >= 0)
        u += "&zon=";
      else
        u += "?zon=";
      u += cfg.zone;
    }
  }

  if ((size_t)u.length() >= n)
    return false;
  strncpy(out, u.c_str(), n - 1);
  out[n - 1] = '\0';
  return true;
}

inline bool takwimTmpFileLooksValid(File &f) {
  f.seek(0, SeekSet);
  char buf[512];
  int rd = f.readBytes(buf, sizeof(buf) - 1);
  if (rd < 40)
    return false;
  buf[rd] = '\0';
  if (strstr(buf, "HIJRI_DATA") != nullptr)
    return true;
  // Corak DD-MM-YYYY pada permulaan baris
  for (int i = 0; i + 10 < rd; i++) {
    if (buf[i] >= '0' && buf[i] <= '9' && buf[i + 3] == '-' && buf[i + 6] == '-')
      return true;
  }
  return false;
}

inline bool downloadTakwimFromConfig(const ConfigTakwim &cfg, String &err) {
  err = "";
  if (WiFi.status() != WL_CONNECTED) {
    err = "tiada_sta_internet";
    return false;
  }

  char url[512];
  if (!takwimResolveDownloadUrl(url, sizeof(url), cfg)) {
    err = "url_tidak_sah";
    return false;
  }

  HTTPClient http;
  http.setTimeout(25000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent("MyClockESP32/1");

  if (!http.begin(url)) {
    err = "http_begin_gagal";
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    err = "HTTP_" + String(code);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    err = "tiada_stream";
    http.end();
    return false;
  }

  // Pastikan FS dimount (jarang gagal jika boot OK)
  if (!SPIFFS.begin(false)) {
    err = "spiffs_tidak_mounted";
    http.end();
    return false;
  }

  const char *tmpPath = "/takwim_new.tmp";
  SPIFFS.remove(tmpPath);

  auto errBebasBait = [](String &base) {
    size_t t = SPIFFS.totalBytes(), u = SPIFFS.usedBytes();
    if (t > u)
      base += ";bebas_bait=" + String((unsigned long)(t - u));
    else
      base += ";bebas_bait=?";
  };

  File f = SPIFFS.open(tmpPath, "w");
  if (!f) {
    // Ruang SPIFFS sempit atau serpihan: padam takwim lama dahulu
    // (takwim.txt diganti semula jika muat turun berjaya selepas ini).
    invalidateTakwimCache();
    SPIFFS.remove("/takwim.txt");
    SPIFFS.remove(tmpPath);
    f = SPIFFS.open(tmpPath, "w");
  }
  if (!f) {
    err = "spiffs_buka_gagal";
    errBebasBait(err);
    http.end();
    return false;
  }

  unsigned long bootRead = millis();
  uint8_t buf[384];
  for (;;) {
    if (millis() - bootRead > 90000UL) {
      err = "timeout_baca";
      break;
    }
    size_t av = stream->available();
    if (av) {
      int rd = stream->readBytes(buf, av > sizeof(buf) ? sizeof(buf) : av);
      if (rd > 0)
        f.write(buf, (size_t)rd);
      continue;
    }
    if (!http.connected())
      break;
    delay(2);
  }

  f.flush();
  f.close();
  http.end();

  if (err.length()) {
    SPIFFS.remove(tmpPath);
    return false;
  }

  f = SPIFFS.open(tmpPath, "r");
  if (!f) {
    err = "baca_semula_gagal";
    SPIFFS.remove(tmpPath);
    return false;
  }
  if (f.size() < 200) {
    f.close();
    err = "fail_terlalu_kecil";
    SPIFFS.remove(tmpPath);
    return false;
  }
  if (!takwimTmpFileLooksValid(f)) {
    f.close();
    err = "format_tidak_sah";
    SPIFFS.remove(tmpPath);
    return false;
  }
  f.close();

  if (SPIFFS.exists("/takwim.txt"))
    SPIFFS.remove("/takwim.txt");
  if (!SPIFFS.rename(tmpPath, "/takwim.txt")) {
    err = "rename_gagal";
    SPIFFS.remove(tmpPath);
    return false;
  }

  return true;
}

#endif
