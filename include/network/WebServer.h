#ifndef MY_CLOCK_WEB_SERVER_H
#define MY_CLOCK_WEB_SERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "core/AudioModule.h"
#include "core/DisplayModule.h"
#include "core/TimeModule.h"
#include "data/ConfigModule.h"
#include "data/TakwimModule.h"
#include "logic/AnnounceModule.h"
#include "network/HTTPDownload.h"
#include "network/NTPManager.h"
#include "network/WiFiManager.h"

// Pembolehubah global (definisi dalam main.cpp)
extern bool clockWebRebootSoon;

inline WebServer &clockWebServer() {
  static WebServer srv(80);
  return srv;
}

static inline String clockWebReadPostBody(WebServer &srv) {
  // application/json → keseluruhan badan diletak sebagai arg "plain".
  // x-www-form-urlencoded → biasanya medan data=...
  String plain = srv.arg("plain");
  String data = srv.arg("data");
  if (plain.length() != 0 && plain.charAt(0) == '{')
    return plain;
  if (data.length() != 0)
    return data;
  if (plain.length() != 0)
    return plain;
  return "";
}

static inline void clockWebSendJson(WebServer &srv, int code,
                                    const String &json) {
  srv.send(code, "application/json; charset=utf-8", json);
}

// Lepaskan handle SPIFFS audio sebelum apa-apa tulis JSON / SPIFFS lain
static inline void clockWebSebelumSimpanSpi() {
  stopAndFlushAudio();
  delay(45);
}

static inline void clockWebHandle404(WebServer &srv) {
  srv.send(404, "text/plain; charset=utf-8", "404");
}

static inline void clockWebHandleRoot(WebServer &srv) {
  File f = SPIFFS.open("/web/index.html", "r");
  if (!f) {
    srv.send(500, "text/plain; charset=utf-8",
             "Tiada /web/index.html — jalankan: pio run -t uploadfs");
    return;
  }
  srv.streamFile(f, "text/html; charset=utf-8");
  f.close();
}

static inline void clockWebHandleStatus(WebServer &srv) {
  JsonDocument doc;
  doc["heap"] = (int)ESP.getFreeHeap();
  doc["ssid"] = WiFi.SSID();
  doc["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
  doc["sta_ip"] =
      (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  doc["ap_ip"] = wifiIsSoftApMode() ? WiFi.softAPIP().toString() : "";
  doc["mode"] = wifiIsSoftApMode() ? "ap" : "sta";

  ConfigWiFi wf = getWiFiConfig();
  doc["mdns"] = String(wifiHostnameOrDefault(wf)) + ".local";

  doc["layout"] = (int)activeLayout;
  doc["tts_lang"] = activeTtsLang;
  doc["takwim_ok"] = takwimTodayValid();
  doc["takwim_zon"] = takwimZoneNameStr();

  DateTime t = rtc.now();
  char iso[36];
  snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d", t.year(),
           t.month(), t.day(), t.hour(), t.minute(), t.second());
  doc["rtc_local"] = iso;

  String out;
  serializeJson(doc, out);
  clockWebSendJson(srv, 200, out);
}

static inline void clockWebHandleGetConfig(WebServer &srv) {
  JsonDocument doc;
  ConfigWiFi w = getWiFiConfig();
  ConfigTakwim tk = getTakwimConfig();
  ConfigDisplay d = getDisplayConfig();
  ConfigAudio a = getAudioConfig();
  ConfigAnnounce an = getAnnounceConfig();

  doc["wifi"]["ssid"] = w.ssid;
  doc["wifi"]["password"] = "";
  doc["wifi"]["hostname"] = w.hostname;
  doc["takwim"]["url"] = tk.url;
  doc["takwim"]["zone"] = tk.zone;
  doc["display"]["layout"] = d.layout;
  doc["audio"]["volume"] = a.volume;
  doc["audio"]["tts_lang"] = a.ttsLang;
  doc["announce"]["prayer"] = an.prayer;
  doc["announce"]["custom"] = an.custom;
  doc["announce"]["every_minute"] = an.everyMinute;
  doc["announce"]["every_quarter"] = an.everyQuarter;

  String out;
  serializeJson(doc, out);
  clockWebSendJson(srv, 200, out);
}

static inline void clockWebHandlePostWifi(WebServer &srv) {
  clockWebSebelumSimpanSpi();
  String body = clockWebReadPostBody(srv);
  if (body.length() == 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"tiada_body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    Serial.printf("Web: wifi JSON gagal — %s\n", jerr.c_str());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"json\"}");
    return;
  }

  ConfigWiFi w = getWiFiConfig();
  if (!doc["ssid"].isNull()) {
    const char *s = doc["ssid"];
    if (s)
      configSafeCopy(w.ssid, sizeof(w.ssid), s);
  }
  if (!doc["password"].isNull()) {
    const char *s = doc["password"];
    if (s && s[0] != '\0')
      configSafeCopy(w.password, sizeof(w.password), s);
  }
  if (!doc["hostname"].isNull()) {
    const char *s = doc["hostname"];
    if (s)
      configSafeCopy(w.hostname, sizeof(w.hostname), s);
  }

  Serial.println(F("Web: simpan wi-fi diproses …"));
  if (!saveWiFiConfig(w)) {
    Serial.println(
        F("Web: ERROR simpan wi-fi gagal (lihat log Config di atas)"));
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"simpan_wifi\"}");
    return;
  }

  Serial.printf("Web: wi-fi OK — ssid='%s' host='%s', reboot menyusul\n",
                w.ssid, w.hostname);
  clockWebRebootSoon = true;
  clockWebSendJson(srv, 200, "{\"ok\":true,\"reboot\":true}");
}

static inline void clockWebHandlePostTakwimCfg(WebServer &srv) {
  clockWebSebelumSimpanSpi();
  String body = clockWebReadPostBody(srv);
  if (body.length() == 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"tiada_body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    Serial.printf("Web: takwim config JSON gagal — %s\n", jerr.c_str());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"json\"}");
    return;
  }

  ConfigTakwim t = getTakwimConfig();
  if (!doc["url"].isNull()) {
    const char *s = doc["url"];
    if (s)
      configSafeCopy(t.url, sizeof(t.url), s);
  }
  if (!doc["zone"].isNull()) {
    const char *s = doc["zone"];
    if (s)
      configSafeCopy(t.zone, sizeof(t.zone), s);
  }

  Serial.println(F("Web: simpan tetapan takwim (URL/zon) …"));
  if (!saveTakwimConfig(t)) {
    Serial.println(F("Web: ERROR simpan takwim.json gagal"));
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"simpan_takwim\"}");
    return;
  }

  Serial.printf("Web: takwim config OK zon=%s\n", t.zone);

  JsonDocument rd;
  rd["ok"] = true;
  rd["url"] = t.url;
  rd["zone"] = t.zone;
  String outSaved;
  serializeJson(rd, outSaved);
  clockWebSendJson(srv, 200, outSaved);
}

static inline void clockWebHandlePostDisplay(WebServer &srv) {
  clockWebSebelumSimpanSpi();
  String body = clockWebReadPostBody(srv);
  if (body.length() == 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"tiada_body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    Serial.printf("Web: paparan JSON gagal — %s\n", jerr.c_str());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"json\"}");
    return;
  }

  ConfigDisplay cfg;
  if (doc["layout"].isNull()) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"layout\"}");
    return;
  }
  cfg.layout = doc["layout"].as<int>();

  if (!setActiveLayoutByIndex(cfg.layout)) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"layout_julat\"}");
    return;
  }

  if (!saveDisplayConfig(cfg)) {
    Serial.println(F("Web: ERROR simpan display.json gagal"));
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"simpan_paparan\"}");
    return;
  }

  Serial.printf("Web: paparan OK — layout=%d aktif di SPIFFS\n", cfg.layout);
  clockWebSendJson(srv, 200, "{\"ok\":true}");
}

static inline void clockWebHandlePostAudio(WebServer &srv) {
  clockWebSebelumSimpanSpi();
  String body = clockWebReadPostBody(srv);
  if (body.length() == 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"tiada_body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    Serial.printf("Web: audio JSON gagal — %s\n", jerr.c_str());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"json\"}");
    return;
  }

  ConfigAudio a = getAudioConfig();
  if (!doc["volume"].isNull())
    a.volume = doc["volume"].as<int>();
  if (!doc["tts_lang"].isNull()) {
    const char *s = doc["tts_lang"];
    if (s)
      configSafeCopy(a.ttsLang, sizeof(a.ttsLang), s);
  }

  applyAudioRuntimeConfig(a.volume, a.ttsLang);

  if (!saveAudioConfig(a)) {
    Serial.println(F("Web: ERROR simpan audio.json gagal"));
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"simpan_audio\"}");
    return;
  }

  Serial.printf("Web: audio OK vol=%d tts='%s'\n", a.volume, a.ttsLang);
  clockWebSendJson(srv, 200, "{\"ok\":true}");
}

static inline void clockWebHandlePostAnnounce(WebServer &srv) {
  clockWebSebelumSimpanSpi();
  String body = clockWebReadPostBody(srv);
  if (body.length() == 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"tiada_body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError jerr = deserializeJson(doc, body);
  if (jerr) {
    Serial.printf("Web: announce JSON gagal — %s\n", jerr.c_str());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"json\"}");
    return;
  }

  ConfigAnnounce an = getAnnounceConfig();
  if (!doc["prayer"].isNull())
    an.prayer = doc["prayer"].as<bool>();
  if (!doc["custom"].isNull())
    an.custom = doc["custom"].as<bool>();
  if (!doc["every_minute"].isNull())
    an.everyMinute = doc["every_minute"].as<bool>();
  if (!doc["every_quarter"].isNull())
    an.everyQuarter = doc["every_quarter"].as<bool>();

  applyAnnounceRuntimeConfig(an.prayer, an.custom, an.everyMinute,
                             an.everyQuarter);

  if (!saveAnnounceConfig(an)) {
    Serial.println(F("Web: ERROR simpan announce.json gagal"));
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"simpan_pengumuman\"}");
    return;
  }

  Serial.printf("Web: umumkan OK prayer=%d custom=%d min=%d suku=%d\n",
                (int)an.prayer, (int)an.custom, (int)an.everyMinute,
                (int)an.everyQuarter);
  clockWebSendJson(srv, 200, "{\"ok\":true}");
}

static inline void clockWebHandleNtp(WebServer &srv) {
  String msg;
  bool ok = clockNtpManualSync(msg);
  JsonDocument doc;
  doc["ok"] = ok;
  doc["msg"] = msg;
  String out;
  serializeJson(doc, out);
  clockWebSendJson(srv, ok ? 200 : 400, out);
}
static inline void clockWebHandleUploadTakwim(WebServer &srv) {
  // Lepaskan handle SPIFFS dari audio (elak bentrok semasa tulis)
  stopAndFlushAudio();
  delay(80);

  // Body: text/plain — datang dalam arg "plain"
  String body = srv.arg("plain");

  // ── Validation ringan ──
  if (body.length() < 5000) {
    Serial.printf("Web: upload takwim ditolak — saiz=%u terlalu kecil\n",
                  (unsigned)body.length());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"saiz_terlalu_kecil\"}");
    return;
  }
  if (body.length() > 100000) {
    Serial.printf("Web: upload takwim ditolak — saiz=%u terlalu besar\n",
                  (unsigned)body.length());
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"saiz_terlalu_besar\"}");
    return;
  }

  // ── Cari corak DD-MM-YYYY dalam 1 KB pertama ──
  bool hasDateLine = false;
  int scanLimit = body.length() < 1024 ? body.length() : 1024;
  for (int i = 0; i < scanLimit - 10; i++) {
    char c0 = body.charAt(i);
    char c1 = body.charAt(i + 1);
    char c2 = body.charAt(i + 2);
    char c3 = body.charAt(i + 3);
    char c5 = body.charAt(i + 5);
    if (c0 >= '0' && c0 <= '9' && c1 >= '0' && c1 <= '9' && c2 == '-' &&
        c3 >= '0' && c3 <= '9' && c5 == '-') {
      hasDateLine = true;
      break;
    }
  }
  if (!hasDateLine) {
    Serial.println(
        F("Web: upload takwim ditolak — corak DD-MM-YYYY tidak dijumpai"));
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"format_tidak_sah\"}");
    return;
  }

  // ── Tulis ke /takwim.txt (atomik: padam dulu kemudian tulis baru) ──
  invalidateTakwimCache();

  if (SPIFFS.exists("/takwim.txt") && !SPIFFS.remove("/takwim.txt")) {
    Serial.println(F("Web: amaran — gagal padam /takwim.txt lama"));
  }

  File f = SPIFFS.open("/takwim.txt", "w");
  if (!f) {
    Serial.println(
        F("Web: ERROR upload takwim — tidak boleh buka /takwim.txt"));
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"buka_gagal\"}");
    return;
  }

  size_t written = f.print(body);
  f.flush();
  f.close();

  if (written < (size_t)body.length()) {
    Serial.printf("Web: ERROR upload takwim — tulis %u/%u bait sahaja\n",
                  (unsigned)written, (unsigned)body.length());
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"tulis_tak_lengkap\"}");
    return;
  }

  // ── Reload data takwim baru ──
  readTakwimZoneName();
  DateTime now = rtc.now();
  loadTakwimForDate(now.day(), now.month(), now.year());
  syncPrayersFromTakwim();

  Serial.printf("Web: upload takwim OK — %u bait, paparan_solat=%s\n",
                (unsigned)written,
                takwimTodayValid() ? "ya" : "TIDAK (format/tahun?)");

  clockWebSendJson(srv, 200, "{\"ok\":true}");
}

static inline void clockWebHandleReboot(WebServer &srv) {
  clockWebRebootSoon = true;
  clockWebSendJson(srv, 200, "{\"ok\":true,\"reboot\":true}");
}
// ================================================================
// HANDLER: List semua file dalam SPIFFS
// GET /api/files → {files: [{path, size}, ...], used, total}
// ================================================================
static inline void clockWebHandleFiles(WebServer &srv) {
  JsonDocument doc;
  JsonArray arr = doc["files"].to<JsonArray>();

  // Walk SPIFFS recursively (depth 2 cukup untuk /config/, /web/)
  std::function<void(const char *)> walk = [&](const char *dirPath) {
    File dir = SPIFFS.open(dirPath);
    if (!dir || !dir.isDirectory()) {
      if (dir)
        dir.close();
      return;
    }
    File f = dir.openNextFile();
    while (f) {
      String fullPath = String(f.path());
      if (f.isDirectory()) {
        walk(fullPath.c_str());
      } else {
        JsonObject obj = arr.add<JsonObject>();
        obj["path"] = fullPath;
        obj["size"] = (uint32_t)f.size();
      }
      f = dir.openNextFile();
    }
    dir.close();
  };

  walk("/");

  doc["used"] = (uint32_t)SPIFFS.usedBytes();
  doc["total"] = (uint32_t)SPIFFS.totalBytes();

  String out;
  serializeJson(doc, out);
  clockWebSendJson(srv, 200, out);
}

// ================================================================
// HANDLER: Get content satu file (text)
// GET /api/file?path=/config/wifi.json
// ================================================================
static inline void clockWebHandleGetFile(WebServer &srv) {
  if (!srv.hasArg("path")) {
    srv.send(400, "text/plain; charset=utf-8", "tiada parameter path");
    return;
  }
  String path = srv.arg("path");

  if (path.length() == 0 || path.charAt(0) != '/') {
    srv.send(400, "text/plain; charset=utf-8", "path tidak sah");
    return;
  }

  // Block path traversal
  if (path.indexOf("..") >= 0) {
    srv.send(400, "text/plain; charset=utf-8", "path traversal disekat");
    return;
  }

  if (!SPIFFS.exists(path)) {
    srv.send(404, "text/plain; charset=utf-8", "fail tidak wujud");
    return;
  }

  File f = SPIFFS.open(path, "r");
  if (!f) {
    srv.send(500, "text/plain; charset=utf-8", "tidak boleh buka");
    return;
  }

  // Limit display 200 KB (selamat, elak OOM)
  if (f.size() > 200 * 1024) {
    f.close();
    srv.send(413, "text/plain; charset=utf-8",
             "fail terlalu besar untuk paparan");
    return;
  }

  // Auto MIME
  const char *mime = "text/plain; charset=utf-8";
  if (path.endsWith(".json"))
    mime = "application/json; charset=utf-8";
  else if (path.endsWith(".html"))
    mime = "text/html; charset=utf-8";
  else if (path.endsWith(".css"))
    mime = "text/css; charset=utf-8";
  else if (path.endsWith(".js"))
    mime = "application/javascript; charset=utf-8";

  srv.streamFile(f, mime);
  f.close();
}

// ================================================================
// HANDLER: Save edit JSON file (PUT)
// PUT /api/file?path=/config/wifi.json + body = new content
// ================================================================
static inline void clockWebHandlePutFile(WebServer &srv) {
  if (!srv.hasArg("path")) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"tiada_path\"}");
    return;
  }
  String path = srv.arg("path");

  if (path.length() == 0 || path.charAt(0) != '/') {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"path_tidak_sah\"}");
    return;
  }
  if (path.indexOf("..") >= 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"traversal\"}");
    return;
  }

  // SECURITY: hanya benarkan edit /config/*.json
  if (!path.startsWith("/config/") || !path.endsWith(".json")) {
    clockWebSendJson(srv, 403,
                     "{\"ok\":false,\"err\":\"hanya_/config/*.json\"}");
    return;
  }

  String body = srv.arg("plain");
  if (body.length() == 0) {
    clockWebSendJson(srv, 400, "{\"ok\":false,\"err\":\"body_kosong\"}");
    return;
  }
  if (body.length() > 8192) {
    clockWebSendJson(srv, 413, "{\"ok\":false,\"err\":\"body_terlalu_besar\"}");
    return;
  }

  // Validate JSON di server side juga (defence-in-depth)
  JsonDocument validateDoc;
  DeserializationError jerr = deserializeJson(validateDoc, body);
  if (jerr) {
    String err = "{\"ok\":false,\"err\":\"json_tak_sah:";
    err += jerr.c_str();
    err += "\"}";
    clockWebSendJson(srv, 400, err);
    return;
  }

  // Lepaskan SPIFFS dari audio sebelum tulis
  stopAndFlushAudio();
  delay(50);

  // Atomik: padam dulu, tulis baru
  if (SPIFFS.exists(path) && !SPIFFS.remove(path)) {
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"padam_lama_gagal\"}");
    return;
  }

  File f = SPIFFS.open(path, "w");
  if (!f) {
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"buka_tulis_gagal\"}");
    return;
  }

  size_t n = f.print(body);
  f.flush();
  f.close();

  if (n != (size_t)body.length()) {
    Serial.printf("Web: file edit — tulis %u/%u bait sahaja\n", (unsigned)n,
                  (unsigned)body.length());
    clockWebSendJson(srv, 500, "{\"ok\":false,\"err\":\"tulis_separuh\"}");
    return;
  }

  Serial.printf("Web: edit fail %s OK (%u bait)\n", path.c_str(), (unsigned)n);
  clockWebSendJson(srv, 200, "{\"ok\":true}");
}

inline void clockWebServerBegin() {
  WebServer &s = clockWebServer();

  s.on("/", HTTP_GET, [&s]() { clockWebHandleRoot(s); });
  s.on("/index.html", HTTP_GET, [&s]() { clockWebHandleRoot(s); });

  s.on("/api/status", HTTP_GET, [&s]() { clockWebHandleStatus(s); });
  s.on("/api/config", HTTP_GET, [&s]() { clockWebHandleGetConfig(s); });

  s.on("/api/config/wifi", HTTP_POST, [&s]() { clockWebHandlePostWifi(s); });
  s.on("/api/config/takwim", HTTP_POST,
       [&s]() { clockWebHandlePostTakwimCfg(s); });
  s.on("/api/config/display", HTTP_POST,
       [&s]() { clockWebHandlePostDisplay(s); });
  s.on("/api/config/audio", HTTP_POST, [&s]() { clockWebHandlePostAudio(s); });
  s.on("/api/config/announce", HTTP_POST,
       [&s]() { clockWebHandlePostAnnounce(s); });

  s.on("/api/action/ntp_sync", HTTP_POST, [&s]() { clockWebHandleNtp(s); });
  s.on("/api/action/reboot", HTTP_POST, [&s]() { clockWebHandleReboot(s); });
  s.on("/api/action/upload_takwim", HTTP_POST,
       [&s]() { clockWebHandleUploadTakwim(s); });

  // File browser
  s.on("/api/files", HTTP_GET, [&s]() { clockWebHandleFiles(s); });
  s.on("/api/file", HTTP_GET, [&s]() { clockWebHandleGetFile(s); });
  s.on("/api/file", HTTP_PUT, [&s]() { clockWebHandlePutFile(s); });

  // Static page
  s.on("/files", HTTP_GET, [&s]() {
    File f = SPIFFS.open("/web/files.html", "r");
    if (!f) {
      s.send(500, "text/plain; charset=utf-8",
             "Tiada /web/files.html — jalankan: pio run -t uploadfs");
      return;
    }
    s.streamFile(f, "text/html; charset=utf-8");
    f.close();
  });
  s.on("/files.html", HTTP_GET, [&s]() {
    File f = SPIFFS.open("/web/files.html", "r");
    if (f) {
      s.streamFile(f, "text/html; charset=utf-8");
      f.close();
    } else {
      s.send(404, "text/plain; charset=utf-8", "404");
    }
  });

  s.onNotFound([&s]() { clockWebHandle404(s); });

  s.begin();
  Serial.println(F("Web: pelayan HTTP :80 (STA atau AP)"));
}

inline void clockWebServerLoop() { clockWebServer().handleClient(); }

#endif
