#pragma once
#include <Arduino.h>
#include "settings.h"

bool netConnect(const Settings& s, uint32_t timeoutMs);
void netDisconnect();
bool netSyncTime(uint32_t timeoutMs);                    // NTP; true if the clock is now sane
bool netGet(const char* url, bool tls, String& body);   // GET -> body on HTTP 200
