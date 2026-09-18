#include "ota.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include "version.h"
#include "power.h"

namespace {

constexpr char NVS_NS[]       = "beachday";
constexpr char KEY_LAST_CHECK[] = "otaLast";   // unix seconds

time_t lastCheckTime() {
  Preferences p;
  if (!p.begin(NVS_NS, true)) return 0;
  time_t t = (time_t)p.getULong64(KEY_LAST_CHECK, 0);
  p.end();
  return t;
}

void rememberCheckTime() {
  time_t now = time(nullptr);
  if (now < 1700000000) return;          // clock not set; don't poison the record
  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  p.putULong64(KEY_LAST_CHECK, (uint64_t)now);
  p.end();
}

bool fetchManifest(const char* url, JsonDocument& doc, char* status, size_t statusLen) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    snprintf(status, statusLen, "manifest: begin failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(status, statusLen, "manifest: HTTP %d", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    snprintf(status, statusLen, "manifest: %s", e.c_str());
    return false;
  }
  return true;
}

// Streams the image into the inactive OTA slot. Returns true only if the whole
// image was written and verified, in which case the caller reboots.
bool downloadAndFlash(const char* url, const char* md5, int expectedSize,
                      char* status, size_t statusLen) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(30000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    snprintf(status, statusLen, "image: begin failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    snprintf(status, statusLen, "image: HTTP %d", code);
    http.end();
    return false;
  }

  int len = http.getSize();
  if (len <= 0) len = expectedSize;
  if (len <= 0) {
    snprintf(status, statusLen, "image: unknown size");
    http.end();
    return false;
  }
  if (expectedSize > 0 && len != expectedSize) {
    // A mismatch usually means the wrong asset was published.
    snprintf(status, statusLen, "image: size %d != manifest %d", len, expectedSize);
    http.end();
    return false;
  }

  if (!Update.begin(len, U_FLASH)) {
    snprintf(status, statusLen, "flash: %s", Update.errorString());
    http.end();
    return false;
  }
  // Catches a corrupted or mis-published binary before it is booted.
  if (md5 && strlen(md5) == 32) Update.setMD5(md5);

  Serial.printf("[ota] writing %d bytes\n", len);
  size_t written = Update.writeStream(*http.getStreamPtr());
  http.end();

  if (written != (size_t)len) {
    snprintf(status, statusLen, "flash: wrote %u of %d", (unsigned)written, len);
    Update.abort();
    return false;
  }
  if (!Update.end()) {
    snprintf(status, statusLen, "flash: %s", Update.errorString());
    return false;
  }
  if (!Update.isFinished()) {
    snprintf(status, statusLen, "flash: not finished");
    return false;
  }
  snprintf(status, statusLen, "updated");
  return true;
}

} // namespace

bool otaDue(const Settings& s) {
  if (!s.otaEnabled || s.otaCheckHours == 0) return false;
  time_t now = time(nullptr);
  if (now < 1700000000) return false;              // no trustworthy clock yet
  time_t last = lastCheckTime();
  if (last == 0) return true;                      // never checked
  return (now - last) >= (time_t)s.otaCheckHours * 3600;
}

OtaResult otaCheck(const Settings& s, bool force, char* status, size_t statusLen) {
  snprintf(status, statusLen, "skipped");
  if (!s.otaEnabled) { snprintf(status, statusLen, "disabled"); return OtaResult::Skipped; }
  if (s.otaManifestUrl[0] == '\0') { snprintf(status, statusLen, "no manifest url"); return OtaResult::Skipped; }
  if (!force && !otaDue(s)) return OtaResult::Skipped;
  if (WiFi.status() != WL_CONNECTED) { snprintf(status, statusLen, "offline"); return OtaResult::Skipped; }

  // Never start a flash the battery might not finish.
  int pct = batteryPercent(batteryVolts());
  if (pct >= 0 && pct < s.otaMinBatteryPct) {
    snprintf(status, statusLen, "battery %d%% < %d%%", pct, s.otaMinBatteryPct);
    return OtaResult::Skipped;
  }

  JsonDocument doc;
  if (!fetchManifest(s.otaManifestUrl, doc, status, statusLen)) return OtaResult::Failed;
  rememberCheckTime();

  const char* want = doc["version"] | "";
  const char* url  = doc["url"]     | "";
  const char* md5  = doc["md5"]     | "";
  int size         = doc["size"]    | 0;
  const char* notes = doc["notes"]  | "";

  if (want[0] == '\0' || url[0] == '\0') {
    snprintf(status, statusLen, "manifest: missing version/url");
    return OtaResult::Failed;
  }
  Serial.printf("[ota] running %s, published %s\n", FIRMWARE_VERSION, want);
  if (strcmp(want, FIRMWARE_VERSION) == 0) {
    snprintf(status, statusLen, "up to date (%s)", FIRMWARE_VERSION);
    return OtaResult::UpToDate;
  }
  if (notes[0]) Serial.printf("[ota] notes: %s\n", notes);

  if (!downloadAndFlash(url, md5, size, status, statusLen)) return OtaResult::Failed;

  Serial.printf("[ota] %s -> %s, rebooting\n", FIRMWARE_VERSION, want);
  Serial.flush();
  delay(200);
  ESP.restart();
  return OtaResult::Updated;   // not reached
}
