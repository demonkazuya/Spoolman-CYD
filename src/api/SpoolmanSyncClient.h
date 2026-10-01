#pragma once

#include <Arduino.h>
#include <vector>

namespace spoolman_api {

struct TraySummary {
  uint8_t trayNumber = 0;
  String uniqueId;
  String name;
  String material;
  String color;
  bool assigned = false;
  uint32_t spoolId = 0;
  String spoolName;
  float remainingWeight = -1;
};

struct AmsSummary {
  String name;
  uint8_t amsNumber = 0;
  std::vector<TraySummary> trays;
};

struct PrinterSummary {
  String name;
  String state;
  uint8_t amsCount = 0;
  uint8_t trayCount = 0;
  uint8_t loadedCount = 0;
  std::vector<AmsSummary> amsUnits;
  std::vector<TraySummary> externalSpools;
};

struct SpoolSummary {
  uint32_t id = 0;
  String name;
  String material;
  String vendor;
  float remainingWeight = -1;
};

bool getPrinters(std::vector<PrinterSummary> &printers, String &error);
bool getSpools(std::vector<SpoolSummary> &spools, String &error);
bool assignSpool(uint32_t spoolId, const String &trayId, String &error);
bool unassignSpool(uint32_t spoolId, String &error);
bool testConnection(const String &baseUrl, String &error);

}  // namespace spoolman_api
