#include "ServerConfig.h"

#include <Preferences.h>

namespace server_config {
namespace {
Preferences preferences;
String currentHost;
String currentPort;
}  // namespace

void begin() {
  preferences.begin("spoolserver", false);
  currentHost = preferences.getString("host", "");
  currentPort = preferences.getString("port", "");
}

String host() { return currentHost; }
String port() { return currentPort; }
bool configured() { return !currentHost.isEmpty() && !currentPort.isEmpty(); }

String baseUrl() {
  if (!configured()) return "";
  return "http://" + currentHost + ":" + currentPort;
}

void save(const String &newHost, const String &newPort) {
  preferences.putString("host", newHost);
  preferences.putString("port", newPort);
  currentHost = newHost;
  currentPort = newPort;
}

}  // namespace server_config
