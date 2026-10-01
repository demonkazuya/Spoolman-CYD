#include "SpoolmanSyncClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "network/ServerConfig.h"

namespace spoolman_api {

bool testConnection(const String &baseUrl, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi first.";
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);
  if (!http.begin(baseUrl + "/api/printers")) {
    error = "Could not start HTTP request.";
    return false;
  }
  const int status = http.GET();
  const String responseBody = status == HTTP_CODE_OK ? http.getString() : "";
  http.end();
  if (status != HTTP_CODE_OK) {
    error = status > 0 ? "Server returned HTTP " + String(status)
                       : "Could not reach that server and port.";
    return false;
  }
  if (responseBody.isEmpty()) {
    error = "The printers API returned an empty response.";
    return false;
  }

  JsonDocument filter;
  filter["printers"][0]["name"] = true;
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(
      response, responseBody, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(12));
  if (parseError || !response["printers"].is<JsonArray>()) {
    error = "Connected, but this is not a valid SpoolmanSync printers API.";
    return false;
  }
  return true;
}

bool getPrinters(std::vector<PrinterSummary> &printers, String &error) {
  printers.clear();
  if (!server_config::configured()) {
    error = "Configure the SpoolmanSync server first.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi first.";
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);
  const String url = server_config::baseUrl() + "/api/printers";
  if (!http.begin(url)) {
    error = "Could not start HTTP request.";
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    error = status > 0 ? "Server returned HTTP " + String(status)
                       : "Could not reach SpoolmanSync.";
    http.end();
    return false;
  }

  // The server uses Transfer-Encoding: chunked. HTTPClient decodes the chunks
  // when getString() reads the response; parsing getStream() directly exposes
  // the chunk framing to ArduinoJson and causes InvalidInput.
  const String responseBody = http.getString();
  http.end();
  if (responseBody.isEmpty()) {
    error = "SpoolmanSync returned an empty printer response.";
    return false;
  }

  // Only retain fields shown on the Printers screen. The full response nests
  // spool, filament, vendor, and tag records much more deeply than we need.
  JsonDocument filter;
  filter["printers"][0]["name"] = true;
  filter["printers"][0]["state"] = true;
  filter["printers"][0]["ams_units"][0]["name"] = true;
  filter["printers"][0]["ams_units"][0]["ams_number"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["tray_number"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["unique_id"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["name"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["material"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["color"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["assigned_spool"]["id"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["assigned_spool"]["remaining_weight"] = true;
  filter["printers"][0]["ams_units"][0]["trays"][0]["assigned_spool"]["filament"]["name"] = true;
  filter["printers"][0]["external_spools"][0]["tray_number"] = true;
  filter["printers"][0]["external_spools"][0]["unique_id"] = true;
  filter["printers"][0]["external_spools"][0]["name"] = true;
  filter["printers"][0]["external_spools"][0]["material"] = true;
  filter["printers"][0]["external_spools"][0]["color"] = true;
  filter["printers"][0]["external_spools"][0]["assigned_spool"]["id"] = true;
  filter["printers"][0]["external_spools"][0]["assigned_spool"]["remaining_weight"] = true;
  filter["printers"][0]["external_spools"][0]["assigned_spool"]["filament"]["name"] = true;

  JsonDocument document;
  const DeserializationError parseError = deserializeJson(
      document, responseBody, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(20));
  if (parseError) {
    error = "Invalid printer response: " + String(parseError.c_str());
    return false;
  }

  JsonArrayConst list = document["printers"].as<JsonArrayConst>();
  if (list.isNull()) {
    error = "Response has no printers list.";
    return false;
  }

  for (JsonObjectConst printer : list) {
    PrinterSummary item;
    item.name = printer["name"] | "Unnamed printer";
    item.state = printer["state"] | "";
    JsonArrayConst units = printer["ams_units"].as<JsonArrayConst>();
    item.amsCount = units.size();
    for (JsonObjectConst unit : units) {
      AmsSummary ams;
      ams.name = unit["name"] | "AMS";
      ams.amsNumber = unit["ams_number"] | 0;
      JsonArrayConst trays = unit["trays"].as<JsonArrayConst>();
      for (JsonObjectConst tray : trays) {
        TraySummary slot;
        slot.trayNumber = tray["tray_number"] | 0;
        slot.uniqueId = tray["unique_id"] | "";
        slot.name = tray["name"] | "Tray";
        slot.material = tray["material"] | "";
        slot.color = tray["color"] | "";
        JsonObjectConst assigned = tray["assigned_spool"].as<JsonObjectConst>();
        slot.assigned = !assigned.isNull();
        if (slot.assigned) {
          slot.spoolId = assigned["id"] | 0;
          slot.remainingWeight = assigned["remaining_weight"] | -1.0f;
          slot.spoolName = assigned["filament"]["name"] | "Spool";
          ++item.loadedCount;
        }
        ams.trays.push_back(slot);
        ++item.trayCount;
      }
      item.amsUnits.push_back(ams);
    }
    JsonArrayConst external = printer["external_spools"].as<JsonArrayConst>();
    for (JsonObjectConst slot : external) {
      TraySummary externalSlot;
      externalSlot.trayNumber = slot["tray_number"] | 0;
      externalSlot.uniqueId = slot["unique_id"] | "";
      externalSlot.name = slot["name"] | "External spool";
      externalSlot.material = slot["material"] | "";
      externalSlot.color = slot["color"] | "";
      JsonObjectConst assigned = slot["assigned_spool"].as<JsonObjectConst>();
      externalSlot.assigned = !assigned.isNull();
      if (externalSlot.assigned) {
        externalSlot.spoolId = assigned["id"] | 0;
        externalSlot.remainingWeight = assigned["remaining_weight"] | -1.0f;
        externalSlot.spoolName = assigned["filament"]["name"] | "Spool";
        ++item.loadedCount;
      }
      item.externalSpools.push_back(externalSlot);
      ++item.trayCount;
    }
    printers.push_back(item);
  }
  return true;
}

bool getSpools(std::vector<SpoolSummary> &spools, String &error) {
  spools.clear();
  if (!server_config::configured()) {
    error = "Configure the SpoolmanSync server first.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi first.";
    return false;
  }

  HTTPClient http;
  http.setTimeout(7000);
  const String url = server_config::baseUrl() + "/api/spools";
  if (!http.begin(url)) {
    error = "Could not start HTTP request.";
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    error = status > 0 ? "Server returned HTTP " + String(status)
                       : "Could not reach SpoolmanSync.";
    http.end();
    return false;
  }

  const String responseBody = http.getString();
  http.end();
  if (responseBody.isEmpty()) {
    error = "SpoolmanSync returned an empty spool response.";
    return false;
  }

  JsonDocument filter;
  filter["spools"][0]["id"] = true;
  filter["spools"][0]["remaining_weight"] = true;
  filter["spools"][0]["filament"]["name"] = true;
  filter["spools"][0]["filament"]["material"] = true;
  filter["spools"][0]["filament"]["vendor"]["name"] = true;

  JsonDocument document;
  const DeserializationError parseError = deserializeJson(
      document, responseBody, DeserializationOption::Filter(filter),
      DeserializationOption::NestingLimit(12));
  if (parseError) {
    error = "Invalid spool response: " + String(parseError.c_str());
    return false;
  }

  JsonArrayConst list = document["spools"].as<JsonArrayConst>();
  if (list.isNull()) {
    error = "Response has no spools list.";
    return false;
  }

  for (JsonObjectConst spool : list) {
    SpoolSummary item;
    item.id = spool["id"] | 0;
    item.remainingWeight = spool["remaining_weight"] | -1.0f;
    item.name = spool["filament"]["name"] | "Unnamed filament";
    item.material = spool["filament"]["material"] | "Unknown material";
    item.vendor = spool["filament"]["vendor"]["name"] | "";
    spools.push_back(item);
  }
  return true;
}

bool assignSpool(uint32_t spoolId, const String &trayId, String &error) {
  if (!server_config::configured()) {
    error = "Configure the SpoolmanSync server first.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi first.";
    return false;
  }
  if (spoolId == 0 || trayId.isEmpty()) {
    error = "A valid spool ID and tray ID are required.";
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);
  const String url = server_config::baseUrl() + "/api/spools";
  if (!http.begin(url)) {
    error = "Could not start HTTP request.";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  JsonDocument request;
  request["spoolId"] = spoolId;
  request["trayId"] = trayId;
  String requestBody;
  serializeJson(request, requestBody);

  const int status = http.POST(requestBody);
  const String responseBody = http.getString();
  http.end();
  if (status < 200 || status >= 300) {
    JsonDocument errorDocument;
    JsonDocument errorFilter;
    errorFilter["error"] = true;
    if (!responseBody.isEmpty() &&
        !deserializeJson(errorDocument, responseBody,
                         DeserializationOption::Filter(errorFilter),
                         DeserializationOption::NestingLimit(8)) &&
        !errorDocument["error"].isNull()) {
      error = errorDocument["error"].as<String>();
    } else {
      error = status > 0 ? "Server returned HTTP " + String(status)
                         : "Could not reach SpoolmanSync.";
    }
    return false;
  }

  if (responseBody.isEmpty()) {
    error = "Server returned success without the updated spool.";
    return false;
  }
  JsonDocument responseFilter;
  responseFilter["spool"]["id"] = true;
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(
      response, responseBody, DeserializationOption::Filter(responseFilter),
      DeserializationOption::NestingLimit(12));
  if (parseError || response["spool"]["id"].isNull()) {
    error = "Server response did not confirm the updated spool.";
    return false;
  }
  const uint32_t returnedSpoolId = response["spool"]["id"] | 0;
  if (returnedSpoolId != spoolId) {
    error = "Server confirmed a different spool ID.";
    return false;
  }
  return true;
}

bool unassignSpool(uint32_t spoolId, String &error) {
  if (!server_config::configured()) {
    error = "Configure the SpoolmanSync server first.";
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi first.";
    return false;
  }
  if (spoolId == 0) {
    error = "A valid spool ID is required.";
    return false;
  }

  HTTPClient http;
  http.setTimeout(8000);
  const String url = server_config::baseUrl() + "/api/spools";
  if (!http.begin(url)) {
    error = "Could not start HTTP request.";
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  JsonDocument request;
  request["spoolId"] = spoolId;
  String requestBody;
  serializeJson(request, requestBody);

  const int status = http.sendRequest("DELETE", requestBody);
  const String responseBody = http.getString();
  http.end();
  if (status < 200 || status >= 300) {
    JsonDocument errorDocument;
    JsonDocument errorFilter;
    errorFilter["error"] = true;
    if (!responseBody.isEmpty() &&
        !deserializeJson(errorDocument, responseBody,
                         DeserializationOption::Filter(errorFilter),
                         DeserializationOption::NestingLimit(8)) &&
        !errorDocument["error"].isNull()) {
      error = errorDocument["error"].as<String>();
    } else {
      error = status > 0 ? "Server returned HTTP " + String(status)
                         : "Could not reach SpoolmanSync.";
    }
    return false;
  }
  if (responseBody.isEmpty()) {
    error = "Server returned success without the updated spool.";
    return false;
  }

  JsonDocument responseFilter;
  responseFilter["spool"]["id"] = true;
  JsonDocument response;
  const DeserializationError parseError = deserializeJson(
      response, responseBody, DeserializationOption::Filter(responseFilter),
      DeserializationOption::NestingLimit(12));
  if (parseError || response["spool"]["id"].isNull()) {
    error = "Server response did not confirm the updated spool.";
    return false;
  }
  const uint32_t returnedSpoolId = response["spool"]["id"] | 0;
  if (returnedSpoolId != spoolId) {
    error = "Server confirmed a different spool ID.";
    return false;
  }
  return true;
}

}  // namespace spoolman_api
