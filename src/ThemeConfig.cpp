#include "ThemeConfig.h"

#include <Preferences.h>

namespace theme_config {
namespace {
Preferences preferences;
bool currentLightMode = false;
}  // namespace

void begin() {
  if (!preferences.begin("uitheme", false)) {
    Serial.println("Theme settings: failed to open NVS; using dark theme.");
    return;
  }
  currentLightMode = preferences.getBool("light", false);
  Serial.printf("Theme settings loaded: %s.\n", currentLightMode ? "light" : "dark");
}

bool lightMode() { return currentLightMode; }

bool setLightMode(bool enabled) {
  if (preferences.putBool("light", enabled) != sizeof(uint8_t)) {
    Serial.println("Theme settings: failed to save theme.");
    return false;
  }
  currentLightMode = enabled;
  Serial.printf("Theme settings saved: %s.\n", enabled ? "light" : "dark");
  return true;
}

}  // namespace theme_config
