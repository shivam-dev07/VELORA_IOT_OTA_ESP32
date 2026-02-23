/**
 * Velora IoT Platform — ESP32 Full Featured Sketch
 * ==================================================
 * Features:
 *   1. Sends sensor telemetry every 5 s    → POST /api/v1/telemetry/batch
 *   2. Polls dashboard LED command every 2 s → GET  /api/v1/datastreams/:id/value
 *   3. Checks for OTA firmware update every 5 min → GET /api/v1/ota/device-check
 *   4. Downloads & applies firmware using ESP32's built-in Update library
 *   5. Reports OTA progress back to platform → POST /api/v1/ota/device-report
 *
 * Required Arduino libraries (install via Library Manager):
 *   - WiFi         (built-in with ESP32 Arduino core)
 *   - HTTPClient   (built-in with ESP32 Arduino core)
 *   - Update       (built-in with ESP32 Arduino core)
 *   - ArduinoJson  (by Benoit Blanchon, v6 or v7)
 *
 * Board: ESP32 Dev Module (or any ESP32 variant)
 *
 * IMPORTANT: SERVER must be ONLY the base host+port, e.g. "http://192.168.0.100:3001"
 *            The /api/v1 prefix is added automatically in each request below.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>

// ── Wi-Fi credentials ─────────────────────────────────
const char* ssid     = "Raju";
const char* password = "su@shi1907";

// ── Velora server base URL ────────────────────────────
// Local:      "http://192.168.0.100:3001"
// Production: "https://api.yourvelora.com"
// NOTE: Do NOT add /api/v1 here — it is added automatically in each request.
const char* SERVER = "http://192.168.0.100:3001";

// ── Device credentials ────────────────────────────────
// Find in: Dashboard → Devices → (your device) → Security tab
const char* DEVICE_ID    = "5de8f19d-e5e9-4694-8e4f-05dd665e0a45";
const char* DEVICE_TOKEN = "vlr_ab584ba73a1a2a3a9b3eba09599e3cf9c505304cff85e3f6";

// ── Datastream IDs ────────────────────────────────────
// Find in: Dashboard → Devices → (your device) → Datastreams tab
const char* DS_TEMP     = "87ba2583-8a7c-4e38-8437-59c44df20615"; // sensor → platform
const char* DS_HUMIDITY = "3d9e3f70-e98e-4d96-ab60-51451840ce6a"; // sensor → platform
const char* DS_LED_CMD  = "6dfe04cf-abd1-48d0-b088-0a5779b0da43"; // dashboard toggle → device

// ── Current firmware version ──────────────────────────
// MUST match the version you upload on the Firmware page
const char* CURRENT_VERSION = "1.0.0";

// ── Hardware ──────────────────────────────────────────
const int LED_PIN = 8; // built-in LED — ESP32-C3=8, classic ESP32=2

// ── Timing (milliseconds) ─────────────────────────────
const unsigned long TELEMETRY_INTERVAL_MS = 5000;   // telemetry every 5 s
const unsigned long POLL_INTERVAL_MS      = 2000;   // LED command poll every 2 s
const unsigned long OTA_CHECK_INTERVAL_MS = 30000; // OTA check every 5 min

unsigned long lastTelemetryMs = 0;
unsigned long lastPollMs      = 0;
unsigned long lastOtaCheckMs  = 0;

// ─────────────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // most boards: HIGH = LED off (active-low)

  Serial.println("\n[Velora] Booting...");
  connectWiFi();

  // Check for OTA update immediately on boot
  checkAndApplyOTA();
}

// ─────────────────────────────────────────────────────
// loop()
// ─────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // Auto-reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Reconnecting...");
    connectWiFi();
  }

  // 1. Send sensor telemetry
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryMs = now;
    sendTelemetry();
  }

  // 2. Poll LED command from dashboard toggle widget
  if (now - lastPollMs >= POLL_INTERVAL_MS) {
    lastPollMs = now;
    pollLedCommand();
  }

  // 3. Check for OTA firmware update
  if (now - lastOtaCheckMs >= OTA_CHECK_INTERVAL_MS) {
    lastOtaCheckMs = now;
    checkAndApplyOTA();
  }

  delay(10);
}

// ─────────────────────────────────────────────────────
// Wi-Fi connection helper
// ─────────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", ssid);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Failed to connect — will retry next cycle");
  }
}

// ─────────────────────────────────────────────────────
// Add common device auth headers to any HTTPClient
// ─────────────────────────────────────────────────────
void addAuthHeaders(HTTPClient& http) {
  http.addHeader("X-Device-Id",    DEVICE_ID);
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.addHeader("Content-Type",   "application/json");
}

// ─────────────────────────────────────────────────────
// 1. Send sensor data as a batch to Velora
// ─────────────────────────────────────────────────────
void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) return;

  // ── Replace with real sensor reads ──────────────────
  float temperature = 25.3f + (float)(random(-10, 10)) / 10.0f;
  float humidity    = 60.0f + (float)(random(-50, 50)) / 10.0f;
  // ────────────────────────────────────────────────────

  StaticJsonDocument<256> doc;
  JsonArray readings = doc.createNestedArray("readings");

  JsonObject t = readings.createNestedObject();
  t["datastreamId"] = DS_TEMP;
  t["value"]        = temperature;

  JsonObject h = readings.createNestedObject();
  h["datastreamId"] = DS_HUMIDITY;
  h["value"]        = humidity;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(String(SERVER) + "/api/v1/telemetry/batch");
  addAuthHeaders(http);

  int code = http.POST(body);
  if (code == 200 || code == 201) {
    Serial.printf("[Telemetry] Sent OK — temp=%.1f°C  hum=%.1f%%\n", temperature, humidity);
  } else {
    Serial.printf("[Telemetry] Failed: HTTP %d\n", code);
  }
  http.end();
}

// ─────────────────────────────────────────────────────
// 2. Poll LED command written by the dashboard toggle widget
//    Dashboard toggle → POST /api/v1/datastreams/:id/command { value: true }
//    This reads back that stored value so the device acts on it.
// ─────────────────────────────────────────────────────
void pollLedCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = String(SERVER) + "/api/v1/datastreams/" + DS_LED_CMD + "/value";
  http.begin(url);
  http.addHeader("X-Device-Id",    DEVICE_ID);
  http.addHeader("X-Device-Token", DEVICE_TOKEN);

  int code = http.GET();
  if (code == 200) {
    String payload = http.getString();
    StaticJsonDocument<128> doc;
    deserializeJson(doc, payload);
    // Response: { "success": true, "data": { "value": true/false/null } }
    bool ledOn = doc["data"]["value"] | false;
    // Active-low: LOW = on, HIGH = off
    digitalWrite(LED_PIN, ledOn ? LOW : HIGH);
    Serial.printf("[LED] %s\n", ledOn ? "ON" : "OFF");
  } else {
    Serial.printf("[LED] Poll failed: HTTP %d\n", code);
  }
  http.end();
}

// ─────────────────────────────────────────────────────
// Helper: report OTA progress / result back to Velora
// ─────────────────────────────────────────────────────
void reportOTA(const String& deploymentId, const String& status,
               int progress = -1, const String& errorMsg = "",
               const String& version = "") {
  if (WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<256> doc;
  doc["deploymentId"] = deploymentId;
  doc["status"]       = status;
  if (progress >= 0)     doc["progress"]     = progress;
  if (errorMsg.length()) doc["errorMessage"] = errorMsg;
  if (version.length())  doc["version"]      = version;

  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(String(SERVER) + "/api/v1/ota/device-report");
  addAuthHeaders(http);
  http.POST(body);
  http.end();
}

// ─────────────────────────────────────────────────────
// 3. Check Velora for a pending OTA update and apply it
// ─────────────────────────────────────────────────────
void checkAndApplyOTA() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.printf("[OTA] Checking for update (current: v%s)...\n", CURRENT_VERSION);

  // ── Step 1: Ask server if there is a pending update ──
  HTTPClient http;
  String checkUrl = String(SERVER) + "/api/v1/ota/device-check?version=" + CURRENT_VERSION;
  http.begin(checkUrl);
  http.addHeader("X-Device-Id",    DEVICE_ID);
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  int code = http.GET();

  if (code != 200) {
    Serial.printf("[OTA] Check failed: HTTP %d\n", code);
    http.end();
    return;
  }

  String response = http.getString();
  http.end();

  // ── Step 2: Parse response ───────────────────────────
  StaticJsonDocument<512> doc;
  DeserializationError jsonErr = deserializeJson(doc, response);
  if (jsonErr) {
    Serial.printf("[OTA] JSON parse error: %s\n", jsonErr.c_str());
    return;
  }

  bool hasUpdate = doc["data"]["update"] | false;
  if (!hasUpdate) {
    Serial.println("[OTA] Up to date.");
    return;
  }

  const char* newVersion   = doc["data"]["version"]      | "";
  const char* firmwareUrl  = doc["data"]["url"]          | "";
  const char* sha256       = doc["data"]["sha256"]       | "";
  int         firmwareSize = doc["data"]["size"]         | 0;
  const char* deploymentId = doc["data"]["deploymentId"] | "";

  Serial.printf("[OTA] Update available: v%s (%d bytes)\n", newVersion, firmwareSize);
  Serial.printf("[OTA] URL: %s\n", firmwareUrl);

  // ── Step 3: Download firmware .bin ───────────────────
  reportOTA(deploymentId, "downloading", 0);

  HTTPClient dlHttp;
  dlHttp.begin(firmwareUrl);
  dlHttp.setTimeout(60000);  // 60s download timeout

  // Follow redirects (needed for GitHub Releases / S3 signed URLs)
  dlHttp.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int dlCode = dlHttp.GET();
  if (dlCode != 200) {
    Serial.printf("[OTA] Download failed: HTTP %d\n", dlCode);
    reportOTA(deploymentId, "failed", -1, "Download HTTP " + String(dlCode));
    dlHttp.end();
    return;
  }

  int contentLength = dlHttp.getSize();
  if (contentLength <= 0 && firmwareSize > 0) {
    contentLength = firmwareSize;
  }

  Serial.printf("[OTA] Downloading %d bytes...\n", contentLength);

  // ── Step 4: Write firmware via Update library ─────────
  if (!Update.begin(contentLength > 0 ? contentLength : UPDATE_SIZE_UNKNOWN)) {
    String errMsg = "Update.begin failed: " + String(Update.errorString());
    Serial.println("[OTA] " + errMsg);
    reportOTA(deploymentId, "failed", -1, errMsg);
    dlHttp.end();
    return;
  }

  reportOTA(deploymentId, "applying", 10);

  WiFiClient* stream = dlHttp.getStreamPtr();
  uint8_t  buf[1024];
  int      written      = 0;
  int      lastReported = 0;

  while (dlHttp.connected() && (contentLength > 0 ? written < contentLength : true)) {
    int available = stream->available();
    if (available > 0) {
      int toRead = min((int)sizeof(buf), available);
      int n      = stream->readBytes(buf, toRead);
      if (n > 0) {
        if (Update.write(buf, n) != (size_t)n) {
          String errMsg = "Write error: " + String(Update.errorString());
          Serial.println("[OTA] " + errMsg);
          reportOTA(deploymentId, "failed", -1, errMsg);
          dlHttp.end();
          return;
        }
        written += n;

        // Report every 10% progress
        if (contentLength > 0) {
          int pct = (written * 100) / contentLength;
          if (pct - lastReported >= 10) {
            lastReported = pct;
            Serial.printf("[OTA] Progress: %d%%\n", pct);
            reportOTA(deploymentId, "applying", pct);
          }
        }
      }
    } else {
      delay(1);
    }
  }

  dlHttp.end();

  // ── Step 5: Verify and finalize ───────────────────────
  reportOTA(deploymentId, "verifying", 95);

  if (!Update.end(true)) {
    String errMsg = "Update.end failed: " + String(Update.errorString());
    Serial.println("[OTA] " + errMsg);
    reportOTA(deploymentId, "failed", -1, errMsg);
    return;
  }

  Serial.printf("[OTA] Success! New firmware: v%s — rebooting in 3s...\n", newVersion);
  reportOTA(deploymentId, "completed", 100, "", String(newVersion));

  delay(3000);
  ESP.restart();
}
