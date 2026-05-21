#ifndef TTS_GOOGLE_FETCH_H
#define TTS_GOOGLE_FETCH_H

// Stream Google TTS (translate_tts) → fail MP3 pada fs luaran.
// Perlu WiFi + heap untuk TLS.

#include <Arduino.h>
#include <FS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

static inline String ttsUrlEncode(const char *s) {
  String e;
  if (!s)
    return e;
  for (; *s; ++s) {
    char c = *s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~')
      e += c;
    else if (c == ' ')
      e += '+';
    else {
      char h[4];
      snprintf(h, sizeof(h), "%%%02X", (unsigned char)c);
      e += h;
    }
  }
  return e;
}

// Pendekkan teks untuk elak URL terlalu panjang (had Google ~200 aksara)
static inline void ttsClampText(const char *src, char *dst, size_t cap) {
  if (!dst || cap < 4)
    return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  size_t n = strlen(src);
  if (n >= cap)
    n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Tulis stream HTTP → fail (atomik .tmp kemudian rename relPath)
inline bool fetchGoogleTtsToFile(fs::FS &fs, const char *relPath,
                                 const char *text, const char *lang) {
  if (WiFi.status() != WL_CONNECTED || !text || !lang)
    return false;

  char textBuf[256];
  ttsClampText(text, textBuf, sizeof(textBuf));

  String url = "https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=";
  url += lang;
  url += "&q=";
  url += ttsUrlEncode(textBuf);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20000);

  HTTPClient http;
  http.setTimeout(25000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setUserAgent("Mozilla/5.0 (compatible; MyClockESP32/1)");
  if (!http.begin(client, url)) {
    Serial.println(F("TTS: http.begin gagal"));
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("TTS: HTTP %d\n", code);
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  if (!stream) {
    http.end();
    return false;
  }

  char tmpPath[128];
  snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", relPath);

  if (fs.exists(tmpPath))
    fs.remove(tmpPath);
  File f = fs.open(tmpPath, "w", true);
  if (!f) {
    Serial.printf("TTS: tidak boleh tulis %s\n", tmpPath);
    http.end();
    return false;
  }

  uint8_t buf[512];
  unsigned long t0 = millis();
  while (http.connected() && (millis() - t0 < 60000UL)) {
    size_t av = stream->available();
    if (av) {
      int rd = stream->readBytes(
          buf, av > sizeof(buf) ? sizeof(buf) : av);
      if (rd > 0)
        f.write(buf, (size_t)rd);
    } else {
      delay(10);
    }
    if (!stream->available() && !http.connected())
      break;
  }
  f.flush();
  f.close();
  http.end();

  File chk = fs.open(tmpPath, "r");
  if (!chk || chk.size() < 256) {
    if (chk)
      chk.close();
    fs.remove(tmpPath);
    Serial.println(F("TTS: fail TTS terlalu kecil / tiada"));
    return false;
  }
  size_t sz = chk.size();
  chk.close();

  if (fs.exists(relPath))
    fs.remove(relPath);
  if (!fs.rename(tmpPath, relPath)) {
    fs.remove(tmpPath);
    Serial.println(F("TTS: rename gagal"));
    return false;
  }
  Serial.printf("TTS: OK %s (%u bait)\n", relPath, (unsigned)sz);
  return true;
}

#endif
