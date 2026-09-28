// Just enough Arduino to compile the render stack on a Mac. The layout, text
// and icon code touches almost nothing from the framework - one Serial.printf
// in text.cpp - so the preview links the real render.cpp rather than a copy
// that could drift away from what the device draws.
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>

struct PreviewSerial {
  void begin(unsigned long = 0) {}
  void flush() {}
  int  available() { return 0; }
  template <typename... A> void printf(const char* f, A... a) { std::fprintf(stderr, f, a...); }
  void print(const char* s)   { std::fputs(s, stderr); }
  void println(const char* s) { std::fputs(s, stderr); std::fputc('\n', stderr); }
  void println()              { std::fputc('\n', stderr); }
  explicit operator bool() const { return true; }
};
extern PreviewSerial Serial;
