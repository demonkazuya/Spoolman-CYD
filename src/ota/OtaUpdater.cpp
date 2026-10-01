#include "OtaUpdater.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "FirmwareVersion.h"

namespace ota_updater {
namespace {

constexpr char kManifestUrl[] =
    "https://github.com/" SPOOLMAN_CYD_REPOSITORY "/releases/latest/download/ota.json";
constexpr char kFirmwareUrlPrefix[] =
    "https://github.com/" SPOOLMAN_CYD_REPOSITORY "/releases/download/";
constexpr uint32_t kTimeSyncTimeoutMs = 12000;
constexpr uint32_t kDownloadIdleTimeoutMs = 20000;

const char kTrustedRoots[] = R"CERT(
-----BEGIN CERTIFICATE-----
MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT
Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg
VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm
aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo
I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng
o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G
A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD
VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB
zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW
RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----

)CERT";

bool ensureClock(String &error) {
  if (time(nullptr) > 1710000000) return true;
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  const uint32_t start = millis();
  while (millis() - start < kTimeSyncTimeoutMs) {
    if (time(nullptr) > 1710000000) return true;
    delay(100);
  }
  error = "Internet time sync failed; cannot verify GitHub TLS certificate.";
  return false;
}

bool parseVersion(String version, uint32_t parts[3]) {
  version.trim();
  if (version.startsWith("v") || version.startsWith("V")) version.remove(0, 1);
  size_t component = 0;
  size_t digitCount = 0;
  parts[0] = parts[1] = parts[2] = 0;
  for (size_t i = 0; i < version.length(); ++i) {
    const char ch = version[i];
    if (ch >= '0' && ch <= '9') {
      if (++digitCount > 9) return false;
      parts[component] = parts[component] * 10 + (ch - '0');
    } else if (ch == '.' && component < 2 && digitCount > 0) {
      ++component;
      digitCount = 0;
    } else {
      return false;
    }
  }
  return component == 2 && digitCount > 0;
}

bool newerVersion(const String &candidate, const String &current) {
  uint32_t next[3], active[3];
  if (!parseVersion(candidate, next) || !parseVersion(current, active))
    return false;
  for (size_t i = 0; i < 3; ++i) {
    if (next[i] != active[i]) return next[i] > active[i];
  }
  return false;
}

bool validSha256(const String &value) {
  if (value.length() != 64) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
          (ch >= 'A' && ch <= 'F')))
      return false;
  }
  return true;
}

void configureHttp(HTTPClient &http) {
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setRedirectLimit(6);
  http.setUserAgent("Spoolman-CYD-OTA/" SPOOLMAN_CYD_VERSION);
}

String hexDigest(const uint8_t digest[32]) {
  static const char digits[] = "0123456789abcdef";
  char result[65];
  for (size_t i = 0; i < 32; ++i) {
    result[i * 2] = digits[digest[i] >> 4];
    result[i * 2 + 1] = digits[digest[i] & 0x0F];
  }
  result[64] = '\0';
  return String(result);
}

}  // namespace

const char *currentVersion() { return SPOOLMAN_CYD_VERSION; }

bool checkForUpdate(Manifest &manifest, bool &updateAvailable, String &error) {
  updateAvailable = false;
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi before checking for updates.";
    return false;
  }
  if (!ensureClock(error)) return false;

  WiFiClientSecure secure;
  secure.setCACert(kTrustedRoots);
  HTTPClient http;
  configureHttp(http);
  if (!http.begin(secure, kManifestUrl)) {
    error = "Could not start the GitHub update check.";
    return false;
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    error = status > 0 ? "GitHub returned HTTP " + String(status)
                       : "Could not reach the GitHub release.";
    http.end();
    return false;
  }

  JsonDocument document;
  const DeserializationError parseError =
      deserializeJson(document, http.getStream(), DeserializationOption::NestingLimit(8));
  http.end();
  if (parseError) {
    error = "Invalid OTA manifest: " + String(parseError.c_str());
    return false;
  }

  manifest.version = document["version"] | "";
  manifest.firmwareUrl = document["firmware_url"] | "";
  manifest.sha256 = document["sha256"] | "";
  manifest.sizeBytes = document["size_bytes"] | 0;
  if (manifest.version.isEmpty() || !newerVersion(manifest.version, currentVersion())) {
    if (manifest.version.isEmpty()) {
      error = "OTA manifest has no valid version.";
      return false;
    }
    return true;
  }
  if (!manifest.firmwareUrl.startsWith(kFirmwareUrlPrefix)) {
    error = "OTA manifest firmware URL is outside the configured GitHub repository.";
    return false;
  }
  if (!validSha256(manifest.sha256) || manifest.sizeBytes == 0) {
    error = "OTA manifest is missing a valid SHA-256 or firmware size.";
    return false;
  }
  updateAvailable = true;
  return true;
}

bool install(const Manifest &manifest, ProgressCallback progress, String &error) {
  if (WiFi.status() != WL_CONNECTED) {
    error = "Connect to Wi-Fi before installing an update.";
    return false;
  }
  if (!ensureClock(error)) return false;
  if (!manifest.firmwareUrl.startsWith(kFirmwareUrlPrefix) ||
      !validSha256(manifest.sha256) || manifest.sizeBytes == 0) {
    error = "The update manifest is invalid.";
    return false;
  }

  WiFiClientSecure secure;
  secure.setCACert(kTrustedRoots);
  HTTPClient http;
  configureHttp(http);
  if (!http.begin(secure, manifest.firmwareUrl)) {
    error = "Could not start the firmware download.";
    return false;
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    error = status > 0 ? "GitHub returned HTTP " + String(status)
                       : "Could not download the firmware from GitHub.";
    http.end();
    return false;
  }
  const int contentLength = http.getSize();
  if (contentLength <= 0 || static_cast<size_t>(contentLength) != manifest.sizeBytes) {
    error = "Downloaded firmware size does not match the manifest.";
    http.end();
    return false;
  }
  if (!Update.begin(manifest.sizeBytes, U_FLASH)) {
    error = "Not enough space in the OTA partition.";
    http.end();
    return false;
  }

  mbedtls_sha256_context hash;
  uint8_t digest[32];
  mbedtls_sha256_init(&hash);
  if (mbedtls_sha256_starts_ret(&hash, 0) != 0) {
    Update.abort();
    mbedtls_sha256_free(&hash);
    http.end();
    error = "Could not initialize firmware verification.";
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t remaining = manifest.sizeBytes;
  size_t completed = 0;
  uint32_t lastDataMs = millis();
  size_t lastProgress = 0;
  while (remaining > 0) {
    const int available = stream->available();
    if (available <= 0) {
      if (!http.connected() || millis() - lastDataMs > kDownloadIdleTimeoutMs) break;
      delay(1);
      continue;
    }
    const size_t chunkSize = min(static_cast<size_t>(available),
                                 min(sizeof(buffer), remaining));
    const size_t received = stream->readBytes(buffer, chunkSize);
    if (received == 0) continue;
    lastDataMs = millis();
    if (Update.write(buffer, received) != received ||
        mbedtls_sha256_update_ret(&hash, buffer, received) != 0) {
      Update.abort();
      mbedtls_sha256_free(&hash);
      http.end();
      error = "Firmware write or verification failed.";
      return false;
    }
    completed += received;
    remaining -= received;
    if (progress != nullptr &&
        (completed - lastProgress >= 32768 || remaining == 0)) {
      progress(completed, manifest.sizeBytes);
      lastProgress = completed;
    }
  }
  http.end();
  if (remaining != 0) {
    Update.abort();
    mbedtls_sha256_free(&hash);
    error = "Firmware download ended before it was complete.";
    return false;
  }
  if (mbedtls_sha256_finish_ret(&hash, digest) != 0) {
    Update.abort();
    mbedtls_sha256_free(&hash);
    error = "Could not finish firmware verification.";
    return false;
  }
  mbedtls_sha256_free(&hash);
  if (!hexDigest(digest).equalsIgnoreCase(manifest.sha256)) {
    Update.abort();
    error = "Firmware SHA-256 verification failed; update was cancelled.";
    return false;
  }
  if (!Update.end()) {
    error = "Firmware could not be committed to the OTA partition.";
    return false;
  }
  return true;
}

}  // namespace ota_updater
