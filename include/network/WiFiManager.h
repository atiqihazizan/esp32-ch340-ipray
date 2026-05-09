#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// Fasa 3: STA dari /config/wifi.json → timeout → AP MyClock-Setup + mDNS.

#include "data/ConfigModule.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

// Paparan/boot — dideklarasikan dalam DisplayModule.h
void showBootStatus(String msg, int progress);
void showWiFiError();

#define WIFI_MANAGER_AP_SSID "MyClock-Setup"
#define WIFI_MANAGER_AP_PASSWORD "12345678"
#define WIFI_STA_CONNECT_TIMEOUT_MS 15000

inline const char *wifiHostnameOrDefault(const ConfigWiFi &cfg) {
  if (cfg.hostname[0] != '\0')
    return cfg.hostname;
  return "my-clock";
}

inline bool wifiIsSoftApMode() { return WiFi.getMode() == WIFI_MODE_AP; }

inline bool wifiIsStaLinked() {
  return WiFi.getMode() == WIFI_MODE_STA && WiFi.status() == WL_CONNECTED;
}

// ── Lepas STA berjaya ATAU selepas SoftAP ── Jalankan sekali bagi setiap fasa rangkaian
inline bool wifiStartMdns(const char *hostNoLocal) {
  if (!MDNS.begin(hostNoLocal)) {
    Serial.printf("mDNS: gagal untuk host '%s'\n", hostNoLocal);
    return false;
  }
  Serial.printf("mDNS: http://%s.local\n", hostNoLocal);
  return true;
}

// Cuba STA 15s; jika gagal → AP + mDNS. Pulangan: true = STA OK, false = dalam mod AP (atau AP gagal sepenuhnya).
inline bool wifiBootStaThenAp(const ConfigWiFi &cfg) {
  const char *mdnsHost = wifiHostnameOrDefault(cfg);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(mdnsHost);
  WiFi.begin(cfg.ssid, cfg.password);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - t0) < (uint32_t)WIFI_STA_CONNECT_TIMEOUT_MS) {
    int p = (int)((millis() - t0) * 90 / WIFI_STA_CONNECT_TIMEOUT_MS) + 5;
    showBootStatus("Connecting WiFi...", p);
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiStartMdns(mdnsHost);
    Serial.printf("WiFi: STA IP %s\n", WiFi.localIP().toString().c_str());
    return true;
  }

  showBootStatus("WiFi gagal → mod AP...", 88);
  delay(600);

  WiFi.disconnect(true, true);
  delay(200);

  WiFi.mode(WIFI_AP);
  IPAddress apIp(192, 168, 4, 1);
  IPAddress gw(192, 168, 4, 1);
  IPAddress mask(255, 255, 255, 0);
  if (!WiFi.softAPConfig(apIp, gw, mask)) {
    Serial.println(F("WiFi: softAPConfig amaran"));
  }

  if (!WiFi.softAP(WIFI_MANAGER_AP_SSID, WIFI_MANAGER_AP_PASSWORD)) {
    Serial.println(F("WiFi: softAP GAGAL"));
    showWiFiError();
    delay(2000);
    return false;
  }

  wifiStartMdns(mdnsHost);
  Serial.printf("WiFi: Hotspot \"%s\" IP %s\n", WIFI_MANAGER_AP_SSID,
                WiFi.softAPIP().toString().c_str());

  showBootStatus("AP: MyClock-Setup OK", 92);
  delay(400);
  return false;
}

#endif
