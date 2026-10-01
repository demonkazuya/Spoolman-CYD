#include "DisplayConfig.h"

#include <Preferences.h>

namespace display_config {
namespace {
Preferences preferences;
bool landscapeMode = false;
}  // namespace

void begin() {
  preferences.begin("display", false);
  landscapeMode = preferences.getBool("landscape", false);
}

bool landscape() { return landscapeMode; }

void setLandscape(bool landscape) {
  preferences.putBool("landscape", landscape);
  landscapeMode = landscape;
}

}  // namespace display_config
