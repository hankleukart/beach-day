// panel.h, for the real boards: forwards every call to whichever driver this
// board's panelModel() names. Chosen once, on first use.
#include "panel.h"
#include "panel_ops.h"
#include "panelmodel.h"

namespace {
const PanelOps* ops = nullptr;
inline const PanelOps& P() {
  if (!ops) ops = panelModel() == PanelModel::E1002 ? &PANEL_E1002 : &PANEL_E1001;
  return *ops;
}
}  // namespace

void panelBegin()     { P().begin(); }
void panelFirstPage() { P().firstPage(); }
bool panelNextPage()  { return P().nextPage(); }
void panelHibernate() { P().hibernate(); }
void panelFill(uint16_t c) { P().fill(c); }
void panelPixel(int x, int y, uint16_t c) { P().pixel(x, y, c); }
void panelHSpan(int x0, int x1, int y, uint16_t c) { P().hspan(x0, x1, y, c); }

bool panelIsColor()     { return P().isColor; }
uint16_t panelInk()     { return P().ink; }
uint16_t panelPaper()   { return P().paper; }
uint16_t panelCaption() { return P().caption; }
uint16_t panelAccent(int role) { return P().accent(role); }
