#pragma once

namespace display_config {

void begin();
bool landscape();
bool setLandscape(bool landscape);
bool redBlueSwapped();
bool colorInverted();
bool setRedBlueSwapped(bool swapped);
bool setColorInverted(bool inverted);

}  // namespace display_config
