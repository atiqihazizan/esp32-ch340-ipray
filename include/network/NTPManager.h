#ifndef NTP_MANAGER_H
#define NTP_MANAGER_H

// Fasa 6: pencetus NTP manual (API web)

#include "core/TimeModule.h"
#include <Arduino.h>
#include <WiFi.h>

inline bool clockNtpManualSync(String &msg) {
  msg = "";
  if (WiFi.status() != WL_CONNECTED) {
    msg = "perlukan_sambungan_sta";
    return false;
  }
  if (!syncNTP()) {
    msg = "ntp_gagal";
    return false;
  }
  msg = "ok";
  return true;
}

#endif
