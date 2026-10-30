#include "panelmodel.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstring>

namespace {
const char* NS = "device";
const char* KEY = "panel";

bool cached = false;
PanelModel current = PanelModel::E1001;

PanelModel parse(const char* s) { return strcmp(s, "e1002") == 0 ? PanelModel::E1002 : PanelModel::E1001; }

PanelModel readStored() {
  Preferences p;
  char buf[8] = "";
  if (p.begin(NS, /*readOnly=*/true)) {
    p.getString(KEY, buf, sizeof(buf));
    p.end();
  }
  return parse(buf);
}
}  // namespace

const char* panelModelId(PanelModel m) { return m == PanelModel::E1002 ? "e1002" : "e1001"; }

const char* panelModelLabel(PanelModel m) {
  return m == PanelModel::E1002 ? "E1002 \xE2\x80\x94 7.3\" colour" : "E1001 \xE2\x80\x94 7.5\" black and white";
}

bool panelSetModel(PanelModel m) {
  Preferences p;
  if (!p.begin(NS, /*readOnly=*/false)) return false;
  char buf[8] = "";
  p.getString(KEY, buf, sizeof(buf));
  bool ok = true;
  if (strcmp(buf, panelModelId(m)) != 0) ok = p.putString(KEY, panelModelId(m)) > 0;   // no needless writes
  p.end();
  if (ok) { current = m; cached = true; }
  return ok;
}

PanelModel panelModel() {
  if (cached) return current;
#if defined(PANEL_PROVISION)
  // A build made for one panel over USB says so on every boot. Writing only
  // when it differs keeps this to a single flash write in the board's life.
  const PanelModel want = (PANEL_PROVISION == 1002) ? PanelModel::E1002 : PanelModel::E1001;
  if (!panelSetModel(want)) { current = want; cached = true; }
  Serial.printf("[panel] provisioned as %s by this build\n", panelModelId(want));
  return current;
#else
  current = readStored();
  cached = true;
  return current;
#endif
}
