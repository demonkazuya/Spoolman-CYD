#pragma once

#include <Arduino.h>

namespace cyd_wifi {

enum class State { NoCredentials, Connecting, Connected, Failed };

void begin();
void poll();
bool saveCredentials(const String &ssid, const String &password);
State state();
bool connected();
bool hasSavedCredentials();
String savedSsid();
const char *stateText();

}  // namespace cyd_wifi
