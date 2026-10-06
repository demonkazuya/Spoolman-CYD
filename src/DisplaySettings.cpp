#include "DisplaySettings.h"

#include <Preferences.h>

namespace display_settings {
namespace {
Preferences preferences;
bool screenSleeping = false;
uint32_t lastActivityTime = 0;
uint16_t currentTimeoutMinutes = 0;
}  // namespace

void begin() {
  if (!preferences.begin("displaysleep", false)) {
    Serial.println("Display sleep settings: failed to open NVS.");
    currentTimeoutMinutes = kDefaultScreenTimeoutMinutes;
    return;
  }
  currentTimeoutMinutes = preferences.getUShort("timeout_min", kDefaultScreenTimeoutMinutes);
  Serial.printf("Display sleep timeout loaded: %u minutes.\n", currentTimeoutMinutes);
}

uint16_t screenTimeoutMinutes() {
  return currentTimeoutMinutes;
}

void setScreenTimeoutMinutes(uint16_t minutes) {
  if (preferences.putUShort("timeout_min", minutes) != sizeof(uint16_t)) {
    Serial.println("Display sleep settings: failed to save timeout.");
    return;
  }
  currentTimeoutMinutes = minutes;
  Serial.printf("Display sleep timeout saved: %u minutes.\n", currentTimeoutMinutes);
}

bool isScreenSleeping() {
  return screenSleeping;
}

void wakeScreen() {
  if (screenSleeping) {
    digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
    screenSleeping = false;
    Serial.println("Display wake: backlight on.");
  }
}

void updateActivity() {
  lastActivityTime = millis();
  wakeScreen();
}

void poll() {
  if (currentTimeoutMinutes == 0) {
    return;  // Never sleep
  }
  
  uint32_t now = millis();
  uint32_t elapsed = now - lastActivityTime;
  uint32_t timeoutMs = (uint32_t)currentTimeoutMinutes * 60 * 1000;
  
  if (!screenSleeping && elapsed >= timeoutMs) {
    digitalWrite(TFT_BL, !TFT_BACKLIGHT_ON);
    screenSleeping = true;
    Serial.printf("Display sleep: backlight off after %u minutes.\n", currentTimeoutMinutes);
  } else if (screenSleeping && elapsed < timeoutMs) {
    // Will be woken by touch handler
  }
}

}  // namespace display_settings
