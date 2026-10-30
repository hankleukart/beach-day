// Which panel this board has. One firmware drives both reTerminals, so the
// board is told once and remembers it in NVS - in its own "device" namespace,
// which "Erase all settings" and a settings wipe leave alone.
//
// Unset means E1001: every board out there before this existed is one.
//
// It gets set two ways:
//   - USB: the per-panel build envs (reterminal_e1002, gift_e1002, dev_e1002 ...)
//     carry -DPANEL_PROVISION=1002 and write it on first boot, so
//     `./flash.sh release e1002` provisions the board as it flashes it.
//     The OTA release build carries no provisioning, so an update never
//     changes a board's mind.
//   - The setup portal (Left Nav), to fix a board set wrong: its screen is
//     blank, but the portal still opens on a phone.
#pragma once
#include <cstdint>

enum class PanelModel : uint8_t { E1001, E1002 };

PanelModel  panelModel();                   // read once, then cached
bool        panelSetModel(PanelModel m);    // false if NVS could not be written
const char* panelModelId(PanelModel m);     // "e1001" / "e1002"
const char* panelModelLabel(PanelModel m);  // for people
