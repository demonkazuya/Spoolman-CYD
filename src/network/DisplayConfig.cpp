#include "DisplayConfig.h"

#include <Preferences.h>

namespace display_config {
namespace {
Preferences preferences;
bool landscapeMode = false;
bool swapRedBlue = false;
bool invertColors = false;
}  // namespace

void begin() {
  if (!preferences.begin("display", false)) {
    Serial.println("Display settings: failed to open NVS; using defaults.");
    return;
  }
  landscapeMode = preferences.getBool("landscape", false);
  swapRedBlue = preferences.getBool("swap_rb", false);
  invertColors = preferences.getBool("invert", false);
  Serial.printf("Display settings loaded: %s, red/blue %s, inversion %s.\n",
                landscapeMode ? "landscape" : "portrait",
                swapRedBlue ? "swapped" : "normal",
                invertColors ? "on" : "off");
}

bool landscape() { return landscapeMode; }

bool setLandscape(bool landscape) {
  if (preferences.putBool("landscape", landscape) != sizeof(uint8_t)) {
    Serial.println("Display settings: failed to save orientation.");
    return false;
  }
  landscapeMode = landscape;
  Serial.printf("Display orientation saved: %s.\n", landscape ? "landscape" : "portrait");
  return true;
}

bool redBlueSwapped() { return swapRedBlue; }
bool colorInverted() { return invertColors; }

bool setRedBlueSwapped(bool swapped) {
  if (preferences.putBool("swap_rb", swapped) != sizeof(uint8_t)) {
    Serial.println("Display settings: failed to save red/blue setting.");
    return false;
  }
  swapRedBlue = swapped;
  Serial.printf("Display red/blue setting saved: %s.\n", swapped ? "swapped" : "normal");
  return true;
}

bool setColorInverted(bool inverted) {
  if (preferences.putBool("invert", inverted) != sizeof(uint8_t)) {
    Serial.println("Display settings: failed to save inversion setting.");
    return false;
  }
  invertColors = inverted;
  Serial.printf("Display inversion setting saved: %s.\n", inverted ? "on" : "off");
  return true;
}

}  // namespace display_config
