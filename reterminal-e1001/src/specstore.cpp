#include "specstore.h"
#include <cstdio>
#include "spec_embedded.h"
#include "icons.h"

namespace { day::Spec g; }

day::Spec& spec() { return g; }

bool specLoad(char* status, size_t n) {
  // Names are checked against the art actually compiled in, not just the
  // spec's own icons[] list.
  day::setIconChecker(iconExists);
  char err[96] = "";
  if (!g.load(SPEC_EMBEDDED, SPEC_EMBEDDED_LEN, err, sizeof(err))) {
    snprintf(status, n, "built-in rules invalid: %s", err);
    return false;
  }
  snprintf(status, n, "built in: %s v%s", g.name(), g.version());
  return true;
}
