#pragma once

#include <Arduino.h>

namespace display_settings {

// Default screen timeout in minutes (0 = never)
constexpr uint16_t kDefaultScreenTimeoutMinutes = 5;

void begin();
uint16_t screenTimeoutMinutes();
void setScreenTimeoutMinutes(uint16_t minutes);
bool isScreenSleeping();
void wakeScreen();
void updateActivity();
void poll();

}  // namespace display_settings
