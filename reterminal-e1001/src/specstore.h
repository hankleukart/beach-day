// The rules are compiled into the firmware: tools/pio_prebuild.py embeds
// shared/v3/day-outcomes.toml in src/spec_embedded.h at build time, so a
// rules change reaches the boards as a firmware release, signed like any other.
#pragma once
#include <cstddef>
#include "dayspec.h"

day::Spec& spec();
bool specLoad(char* status, size_t n);   // false only if the built-in rules fail their checks
