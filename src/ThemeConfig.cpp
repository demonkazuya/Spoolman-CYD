#include "ThemeConfig.h"

#include <Preferences.h>

namespace theme_config {
namespace {
Preferences preferences;
bool currentLightMode = false;
}  // namespace

void begin() {
  preferences.begin("uitheme", false);
  currentLightMode = preferences.getBool("light", false);
}

bool lightMode() { return currentLightMode; }

void setLightMode(bool enabled) {
  preferences.putBool("light", enabled);
  currentLightMode = enabled;
}

}  // namespace theme_config
