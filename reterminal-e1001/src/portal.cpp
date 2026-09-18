#include "portal.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include "render.h"
#include "power.h"
#include "pins.h"
#include "version.h"

namespace {

constexpr uint32_t PORTAL_TIMEOUT_MS = 10UL * 60UL * 1000UL;
constexpr int      MAX_SCAN_ROWS     = 15;

WebServer server(80);
DNSServer dns;
String    networkOptions;     // <option> rows from the pre-AP scan
uint32_t  restartAt = 0;
char      apName[32];
Settings  current;

const char* const DAY_NAMES[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// ---- page --------------------------------------------------------------------
const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Beach Day setup</title>
<style>
body{font:16px -apple-system,system-ui,Segoe UI,Roboto,sans-serif;margin:0;background:#f4f4f2;color:#111}
main{max-width:480px;margin:0 auto;padding:20px 16px 40px}
h1{font-size:26px;margin:8px 0 2px}p.sub{margin:0 0 18px;color:#555}
label{display:block;font-weight:600;margin:16px 0 6px}
input,select{width:100%;box-sizing:border-box;font-size:16px;padding:10px;border:1px solid #bbb;border-radius:8px;background:#fff}
small{color:#666;display:block;margin-top:4px}
button{width:100%;font-size:18px;font-weight:700;padding:14px;border:0;border-radius:10px;background:#111;color:#fff;margin-top:24px}
details{margin-top:22px;border:1px solid #ddd;border-radius:10px;padding:10px 14px;background:#fff}
summary{font-weight:700;cursor:pointer}
fieldset{border:0;padding:0;margin:14px 0 0}legend{font-weight:700;margin-bottom:4px}
.row{display:flex;gap:10px}.row>div{flex:1}
.wk label{display:inline-block;font-weight:400;margin:4px 12px 0 0}.wk input{width:auto;margin-right:4px}
.chk label{display:inline;font-weight:600}.chk input{width:auto;margin-right:6px}
footer{margin-top:36px;font-size:13px;color:#777;text-align:center}footer a{color:#b00}
</style></head><body><main>
<h1>Beach Day setup</h1><p class="sub">Tell this display which Wi-Fi to use and where the beach is.</p>
<form method="POST" action="/save">
<label for="ssid">Wi-Fi network</label>
<select id="ssid" name="ssid" onchange="o.style.display=this.value=='__other__'?'block':'none'">)HTML";

const char PAGE_MID[] PROGMEM = R"HTML(<option value="__other__">Other / hidden network&hellip;</option></select>
<input id="o" name="ssid_other" placeholder="Network name" style="display:none;margin-top:8px">
<label for="pass">Wi-Fi password</label>
<input id="pass" name="pass" type="password" autocomplete="off" placeholder="Leave blank for an open network">
<label for="loc">Where is the beach?</label>
<input id="loc" name="loc" required placeholder="ZIP / postal code, or a town" value="%LOC%">
<small>Examples: <b>90401</b>, <b>Santa Monica</b>, <b>Springfield, IL</b>. The display looks it up once it's online and shows the name it found.</small>
<div class="row"><div><label for="cc">Country</label>
<input id="cc" name="cc" maxlength="2" placeholder="US" value="%CC%"><small>2-letter code. Helps with postal codes.</small></div>
<div><label for="label">Label on screen</label>
<input id="label" name="label" maxlength="22" placeholder="(uses the town name)" value="%LABEL%"></div></div>
<details><summary>Street-cleaning parking reminders</summary>
<small style="margin-top:8px">Shows a NO PARKING bar from sunset the night before until the restriction ends.</small>
%PARKING%
</details>
<button type="submit">Save and restart</button>
</form>
<footer>Beach Day firmware %VER% &middot; <a href="/reset" onclick="return confirm('Erase Wi-Fi, location and parking settings from this display?')">Erase all settings</a></footer>
</main></body></html>)HTML";

String htmlEscape(const char* s) {
  String o;
  for (; *s; s++) {
    switch (*s) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default:  o += *s;
    }
  }
  return o;
}

String hhmm(int minutes) {
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", (minutes / 60) % 24, minutes % 60);
  return String(b);
}

String parkingFieldset(const char* side, const char* title, const beach::ParkingRule& r) {
  String h;
  h += "<fieldset><legend>" + String(title) + "</legend>";
  h += "<div class=\"chk\"><input type=\"checkbox\" id=\"pk" + String(side) + "on\" name=\"pk" + side + "on\"" +
       (r.enabled ? " checked" : "") + "><label for=\"pk" + side + "on\">Enabled</label></div>";
  h += "<label>Weeks of the month</label><div class=\"wk\">";
  for (int w = 1; w <= 5; w++) {
    bool on = r.weeksMask & (1u << (w - 1));
    h += "<label><input type=\"checkbox\" name=\"pk" + String(side) + "w" + w + "\"" + (on ? " checked" : "") + ">" +
         w + (w == 1 ? "st" : w == 2 ? "nd" : w == 3 ? "rd" : "th") + "</label>";
  }
  h += "</div><label>Day</label><select name=\"pk" + String(side) + "day\">";
  for (int d = 0; d < 7; d++) {
    h += "<option value=\"" + String(d) + "\"" + (d == r.dayOfWeek ? " selected" : "") + ">" + DAY_NAMES[d] + "</option>";
  }
  h += "</select><div class=\"row\"><div><label>From</label><input type=\"time\" name=\"pk" + String(side) + "st\" value=\"" + hhmm(r.startMin) + "\"></div>";
  h += "<div><label>Until</label><input type=\"time\" name=\"pk" + String(side) + "end\" value=\"" + hhmm(r.endMin) + "\"></div></div>";
  h += "</fieldset>";
  return h;
}

void handleRoot() {
  String page = FPSTR(PAGE_HEAD);
  page += networkOptions;
  String mid = FPSTR(PAGE_MID);
  mid.replace("%LOC%",   htmlEscape(current.locationQuery));
  mid.replace("%CC%",    htmlEscape(current.countryCode[0] ? current.countryCode : "US"));
  mid.replace("%LABEL%", htmlEscape(current.fromPortal ? current.locationName : ""));
  mid.replace("%PARKING%", parkingFieldset("L", "Left side of the street", current.parking[0]) +
                           parkingFieldset("R", "Right side of the street", current.parking[1]));
  mid.replace("%VER%", FIRMWARE_VERSION);
  page += mid;
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", page);
}

int parseHHMM(const String& s, int fallback) {
  int c = s.indexOf(':');
  if (c < 1) return fallback;
  return s.substring(0, c).toInt() * 60 + s.substring(c + 1).toInt();
}

void readParking(const char* side, beach::ParkingRule& r, const beach::ParkingRule& dflt) {
  String p = String("pk") + side;
  r.side      = dflt.side;
  r.enabled   = server.hasArg(p + "on");
  r.weeksMask = 0;
  for (int w = 1; w <= 5; w++) if (server.hasArg(p + "w" + w)) r.weeksMask |= (1u << (w - 1));
  r.dayOfWeek = (int8_t)constrain(server.arg(p + "day").toInt(), 0, 6);
  r.startMin  = (int16_t)parseHHMM(server.arg(p + "st"),  dflt.startMin);
  r.endMin    = (int16_t)parseHHMM(server.arg(p + "end"), dflt.endMin);
}

void sendSimple(int code, const char* title, const String& body) {
  String page = "<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<style>body{font:17px system-ui,sans-serif;padding:32px 20px;max-width:480px;margin:0 auto;color:#111}h1{font-size:24px}a{color:#06c}</style>"
                "</head><body><h1>" + String(title) + "</h1>" + body + "</body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "text/html", page);
}

void handleSave() {
  PortalInput in;
  String ssid = server.arg("ssid");
  if (ssid == "__other__") ssid = server.arg("ssid_other");
  ssid.trim();
  String loc = server.arg("loc"); loc.trim();
  String cc  = server.arg("cc");  cc.trim(); cc.toUpperCase();
  String label = server.arg("label"); label.trim();

  if (ssid.isEmpty()) { sendSimple(400, "Missing Wi-Fi network", "<p>Pick a network or type its name.</p><p><a href=\"/\">Back</a></p>"); return; }
  if (loc.isEmpty())  { sendSimple(400, "Missing location", "<p>Enter a postal code or a town.</p><p><a href=\"/\">Back</a></p>"); return; }
  if (!cc.isEmpty() && cc.length() != 2) { sendSimple(400, "Country code", "<p>Use a 2-letter code like US, or leave it blank.</p><p><a href=\"/\">Back</a></p>"); return; }

  strlcpy(in.ssid, ssid.c_str(), sizeof(in.ssid));
  strlcpy(in.password, server.arg("pass").c_str(), sizeof(in.password));
  strlcpy(in.locationQuery, loc.c_str(), sizeof(in.locationQuery));
  strlcpy(in.countryCode, cc.c_str(), sizeof(in.countryCode));
  strlcpy(in.label, label.c_str(), sizeof(in.label));
  readParking("L", in.parking[0], current.parking[0]);
  readParking("R", in.parking[1], current.parking[1]);

  if (!savePortalInput(in)) { sendSimple(500, "Couldn't save", "<p>Storage error. Try again.</p><p><a href=\"/\">Back</a></p>"); return; }

  Serial.printf("[portal] saved ssid='%s' loc='%s' cc='%s' label='%s'\n", in.ssid, in.locationQuery, in.countryCode, in.label);
  sendSimple(200, "Saved",
    "<p>The display is restarting. It will join <b>" + htmlEscape(in.ssid) + "</b>, look up <b>" +
    htmlEscape(in.locationQuery) + "</b>, and show the forecast in about a minute.</p>"
    "<p>If it can't find that place, the screen will say so and how to try again.</p>"
    "<p>You can leave this page now.</p>");
  restartAt = millis() + 1500;
}

void handleReset() {
  eraseAllSettings();
  Serial.println("[portal] all settings erased");
  sendSimple(200, "Erased", "<p>All settings were erased. The display is restarting into setup.</p>");
  restartAt = millis() + 1500;
}

// Any URL we don't know - including every OS's captive-portal probe - lands
// on the setup page, which is what makes the phone pop it up automatically.
void handleNotFound() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

String scanNetworksHtml() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  int n = WiFi.scanNetworks(false, false);
  Serial.printf("[portal] scan found %d networks\n", n);

  // Strongest first, one row per SSID, capped, current network preselected.
  struct Row { String ssid; int rssi; bool enc; };
  std::vector<Row> rows;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (Row& r : rows) if (r.ssid == ssid) { dup = true; break; }
    if (dup) continue;
    rows.push_back(Row{ ssid, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN });
  }
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.rssi > b.rssi; });
  if (rows.size() > (size_t)MAX_SCAN_ROWS) rows.resize(MAX_SCAN_ROWS);
  size_t count = rows.size();
  WiFi.scanDelete();

  String h;
  bool currentListed = false;
  for (size_t k = 0; k < count; k++) {
    bool sel = current.configured() && rows[k].ssid == current.ssid;
    currentListed |= sel;
    h += "<option value=\"" + htmlEscape(rows[k].ssid.c_str()) + "\"" + (sel ? " selected" : "") + ">" +
         htmlEscape(rows[k].ssid.c_str()) + (rows[k].enc ? "" : " (open)") + "</option>";
  }
  if (current.configured() && !currentListed) {
    h += "<option value=\"" + htmlEscape(current.ssid) + "\" selected>" + htmlEscape(current.ssid) + " (saved)</option>";
  }
  return h;
}

} // namespace

void runSetupPortal(const Settings& s) {
  current = s;

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(apName, sizeof(apName), "BeachDay-Setup-%02X%02X", mac[4], mac[5]);

  Serial.printf("[portal] starting; hotspot '%s'\n", apName);
  networkOptions = scanNetworksHtml();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(apName);           // open network: it is a ten-minute window
  delay(200);
  IPAddress ip = WiFi.softAPIP();
  dns.start(53, "*", ip);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/reset", HTTP_GET, handleReset);
  server.onNotFound(handleNotFound);
  server.begin();

  renderSetup(apName, ip.toString().c_str());
  Serial.printf("[portal] http://%s/  (timeout %lu min)\n", ip.toString().c_str(), (unsigned long)(PORTAL_TIMEOUT_MS / 60000));

  pinMode(PIN_LED, OUTPUT);
  uint32_t started = millis(), lastBlink = 0;
  bool led = false;
  for (;;) {
    dns.processNextRequest();
    server.handleClient();

    if (restartAt && millis() > restartAt) {
      Serial.println("[portal] restarting");
      Serial.flush();
      ESP.restart();
    }
    if (millis() - started > PORTAL_TIMEOUT_MS) {
      Serial.println("[portal] timed out");
      renderMessage("Setup timed out", "Hold the middle button and press the right one",
                    "to open setup again.");
      renderEnd();
      deepSleepFor(3600);
    }
    if (millis() - lastBlink > 500) {          // fast blink = setup mode
      lastBlink = millis();
      led = !led;
      digitalWrite(PIN_LED, led ? LOW : HIGH);
    }
    delay(5);
  }
}
