#include "net.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

bool netConnect(const Settings& s, uint32_t timeoutMs) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(s.ssid, s.password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) {
      Serial.printf("[net] Wi-Fi timeout (status %d)\n", WiFi.status());
      return false;
    }
    delay(100);
  }
  Serial.printf("[net] connected, ip %s rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

void netDisconnect() {
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
}

bool netSyncTime(uint32_t timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    time_t now = time(nullptr);
    if (now > 1700000000) {      // anything after Nov 2023 means NTP answered
      Serial.printf("[net] ntp ok, utc %ld\n", (long)now);
      return true;
    }
    delay(100);
  }
  Serial.println("[net] ntp timeout");
  return false;
}

bool netGet(const char* url, bool tls, String& body) {
  HTTPClient http;
  http.setTimeout(15000);
  http.setReuse(false);
  http.useHTTP10(true);   // simpler body handling; Open-Meteo is fine with it

  bool began;
  WiFiClientSecure secure;
  WiFiClient plain;
  if (tls) {
    secure.setInsecure();
    began = http.begin(secure, url);
  } else {
    began = http.begin(plain, url);
  }
  if (!began) {
    Serial.println("[net] http.begin failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[net] GET %d for %s\n", code, url);
    http.end();
    return false;
  }
  body = http.getString();
  http.end();
  Serial.printf("[net] got %u bytes\n", body.length());
  return body.length() > 0;
}
