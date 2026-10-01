#pragma once

#include <Arduino.h>

namespace ota_updater {

struct Manifest {
  String version;
  String firmwareUrl;
  String sha256;
  size_t sizeBytes = 0;
};

using ProgressCallback = void (*)(size_t completed, size_t total);

const char *currentVersion();
bool checkForUpdate(Manifest &manifest, bool &updateAvailable, String &error);
bool install(const Manifest &manifest, ProgressCallback progress, String &error);

}  // namespace ota_updater
