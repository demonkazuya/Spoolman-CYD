#pragma once

#include <Arduino.h>

namespace server_config {

void begin();
String host();
String port();
bool configured();
String baseUrl();
void save(const String &host, const String &port);

}  // namespace server_config
