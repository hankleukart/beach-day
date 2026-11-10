// How often the board looks for new firmware on its own, as picked on the
// setup page. Holding Right Nav checks at once whatever this says, and a board
// whose firmware keeps crashing (safe mode) looks every 30 minutes regardless,
// so "Never" cannot strand a broken board.
#pragma once
#include <cstddef>
#include <cstdint>

namespace beach {

struct UpdateCheckOption { uint16_t hours; const char* label; };
constexpr UpdateCheckOption UPDATE_CHECK_OPTIONS[] = {
  { 24, "Daily" }, { 7 * 24, "Weekly" }, { 30 * 24, "Monthly" }, { 0, "Never" },
};
constexpr uint16_t UPDATE_CHECK_DEFAULT_HOURS = 24;

inline bool isUpdateCheckOption(int hours) {
  for (const UpdateCheckOption& o : UPDATE_CHECK_OPTIONS) if (o.hours == hours) return true;
  return false;
}

}  // namespace beach
