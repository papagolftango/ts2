#include "logger_core.h"

#include "ecu_shared.h"

#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <time.h>

namespace {

constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS = 10000;
constexpr uint16_t WEB_RESTART_DELAY_MS = 1200;
constexpr uint32_t TIME_RESYNC_INTERVAL_MS = 3600000;
constexpr uint16_t TIME_SYNC_WAIT_MS = 5000;
constexpr uint32_t VALID_UNIX_EPOCH_MIN = 1700000000UL;

Preferences g_preferences;
WebServer g_webServer(80);
bool g_setupPortalActive = false;
bool g_wifiConnected = false;
bool g_restartRequested = false;
uint32_t g_restartAtMs = 0;
bool g_timeSynced = false;
bool g_timeSetManually = false;
uint32_t g_lastTimeSyncAttemptMs = 0;
uint32_t g_lastTimeSyncSuccessMs = 0;

enum TimeSource : uint8_t {
  TIME_SOURCE_UNKNOWN = 0,
  TIME_SOURCE_NTP = 1,
  TIME_SOURCE_MANUAL = 2,
};

TimeSource g_timeSource = TIME_SOURCE_UNKNOWN;

String formatDeg10(int32_t value) {
  const bool negative = value < 0;
  const int32_t absValue = negative ? -value : value;
  String result = String(absValue / 10);
  result += ".";
  result += String(absValue % 10);
  if (negative) {
    result = "-" + result;
  }
  return result;
}

String wifiIpString() {
  if (g_setupPortalActive) {
    return WiFi.softAPIP().toString();
  }

  if (g_wifiConnected) {
    return WiFi.localIP().toString();
  }

  return String("not connected");
}

bool readCurrentUtc(struct tm &utcOut, time_t &epochOut) {
  time(&epochOut);
  if (epochOut < static_cast<time_t>(VALID_UNIX_EPOCH_MIN)) {
    return false;
  }

  gmtime_r(&epochOut, &utcOut);
  return true;
}

String formatIsoUtc(const struct tm &utcValue) {
  char buf[24];
  snprintf(
    buf,
    sizeof(buf),
    "%04d-%02d-%02dT%02d:%02d:%02dZ",
    utcValue.tm_year + 1900,
    utcValue.tm_mon + 1,
    utcValue.tm_mday,
    utcValue.tm_hour,
    utcValue.tm_min,
    utcValue.tm_sec
  );
  return String(buf);
}

String timeSourceName() {
  if (g_timeSource == TIME_SOURCE_NTP) {
    return String("ntp");
  }

  if (g_timeSource == TIME_SOURCE_MANUAL) {
    return String("manual");
  }

  return String("unsynced");
}

bool syncTimeFromNtp() {
  if (!g_wifiConnected) {
    return false;
  }

  g_lastTimeSyncAttemptMs = millis();
  configTzTime("UTC0", "pool.ntp.org", "time.nist.gov", "time.google.com");

  const uint32_t start = millis();
  struct tm utcNow{};
  time_t epochNow = 0;
  while ((millis() - start) < TIME_SYNC_WAIT_MS) {
    if (readCurrentUtc(utcNow, epochNow)) {
      g_timeSynced = true;
      g_timeSetManually = false;
      g_timeSource = TIME_SOURCE_NTP;
      g_lastTimeSyncSuccessMs = millis();
      Serial.print("Logger time sync OK (UTC): ");
      Serial.println(formatIsoUtc(utcNow));
      return true;
    }
    delay(200);
  }

  return false;
}

void maybeRefreshTimeSync() {
  if (!g_wifiConnected || g_timeSetManually) {
    return;
  }

  const uint32_t nowMs = millis();
  if ((nowMs - g_lastTimeSyncAttemptMs) < TIME_RESYNC_INTERVAL_MS) {
    return;
  }

  syncTimeFromNtp();
}

void loadTelemetrySnapshot(TelemetrySnapshot &snapshot) {
  if (xQueuePeek(telemetryQueue, &snapshot, 0) != pdTRUE) {
    snapshot = TelemetrySnapshot{};
  }
}

void loadStoredWifiCredentials(String &ssid, String &password) {
  g_preferences.begin("wifi", true);
  ssid = g_preferences.getString("ssid", "");
  password = g_preferences.getString("pass", "");
  g_preferences.end();
}

void saveWifiCredentials(const String &ssid, const String &password) {
  g_preferences.begin("wifi", false);
  g_preferences.putString("ssid", ssid);
  g_preferences.putString("pass", password);
  g_preferences.end();
}

bool connectToStationWifi(const String &ssid, const String &password) {
  if (ssid.length() == 0) {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(200);
  }

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  g_wifiConnected = true;
  return true;
}

void startSetupPortal() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ts2-setup", "ts2setup1");
  g_setupPortalActive = true;
  Serial.print("Setup AP started. IP: ");
  Serial.println(WiFi.softAPIP());
}

String buildStatusJson() {
  TelemetrySnapshot snapshot{};
  loadTelemetrySnapshot(snapshot);

  String json = "{";
  json += "\"rpm\":" + String((snapshot.rpm10 + 5U) / 10U) + ",";
  json += "\"rpm10\":" + String(snapshot.rpm10) + ",";
  json += "\"advance_deg\":\"" + formatDeg10(snapshot.advanceDeg10) + "\",";
  json += "\"crank_deg\":\"" + formatDeg10(snapshot.crankAngleDeg10) + "\",";
  json += "\"missing_offset_deg\":\"" + formatDeg10(g_missingToothOffsetDeg10) + "\",";
  json += "\"min_advance_deg\":\"" + formatDeg10(g_minAdvanceDeg10) + "\",";
  json += "\"max_advance_deg\":\"" + formatDeg10(g_maxAdvanceDeg10) + "\",";
  json += "\"cdi_delay_us\":" + String(g_cdiDelayUs) + ",";
  json += "\"dwell_us\":" + String(g_dwellUs) + ",";
  json += "\"strobe_enabled\":" + String(g_strobeEnabled) + ",";
  json += "\"strobe_mask\":" + String(g_strobeMarkerMask) + ",";
  json += "\"strobe_pulse_ms\":" + String(g_strobePulseUs) + ",";
  json += "\"config_version\":" + String(g_configVersion) + ",";
  struct tm utcNow{};
  time_t epochNow = 0;
  const bool hasTime = readCurrentUtc(utcNow, epochNow);
  json += "\"time_synced\":" + String(hasTime ? 1 : 0) + ",";
  json += "\"time_source\":\"" + timeSourceName() + "\",";
  json += "\"epoch\":" + String(static_cast<uint32_t>(hasTime ? epochNow : 0)) + ",";
  json += "\"datetime_utc\":\"" + String(hasTime ? formatIsoUtc(utcNow) : "unsynced") + "\",";
  json += "\"wifi_mode\":\"" + String(g_setupPortalActive ? "setup-ap" : (g_wifiConnected ? "station" : "offline")) + "\",";
  json += "\"ip\":\"" + wifiIpString() + "\"";
  json += "}";
  return json;
}

int32_t argToInt(const String &value, int32_t fallback) {
  if (value.length() == 0) {
    return fallback;
  }

  char *endPtr = nullptr;
  const long parsed = strtol(value.c_str(), &endPtr, 10);
  if (endPtr == value.c_str()) {
    return fallback;
  }

  return static_cast<int32_t>(parsed);
}

void handleRootPage() {
  TelemetrySnapshot snapshot{};
  loadTelemetrySnapshot(snapshot);

  String page =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ts2 Logger</title>"
    "<style>body{font-family:Segoe UI,Arial,sans-serif;background:#f2f5f8;color:#1f2d3a;margin:0;padding:16px;}"
    ".panel{background:#fff;border-radius:10px;padding:14px;margin-bottom:12px;box-shadow:0 2px 8px rgba(0,0,0,.08);}"
    "h1{margin:0 0 10px;font-size:20px;}h2{margin:0 0 8px;font-size:16px;}"
    "table{width:100%;border-collapse:collapse;}td{padding:6px 4px;border-bottom:1px solid #e5ebf0;}"
    "label{display:block;font-size:13px;margin-top:8px;}"
    "input,select{width:100%;padding:8px;margin:6px 0;border:1px solid #c9d5e0;border-radius:6px;}"
    "button{padding:8px 12px;border:0;border-radius:6px;background:#1d6fa5;color:#fff;font-weight:600;cursor:pointer;}"
    "small{color:#4f5f6f;}"
    "</style></head><body>";

  page += "<div class='panel'><h1>ts2 Logger Dashboard</h1>";
  page += "<small>Core 1 web/logger interface. Core 0 ignition path remains isolated.</small></div>";

  page += "<div class='panel'><h2>Network</h2><table>";
  page += "<tr><td>Mode</td><td id='wifi_mode'>" + String(g_setupPortalActive ? "setup-ap" : (g_wifiConnected ? "station" : "offline")) + "</td></tr>";
  page += "<tr><td>IP</td><td id='ip'>" + wifiIpString() + "</td></tr>";
  page += "</table></div>";

  struct tm utcNow{};
  time_t epochNow = 0;
  const bool hasTime = readCurrentUtc(utcNow, epochNow);

  page += "<div class='panel'><h2>Logger Time Metadata</h2><table>";
  page += "<tr><td>UTC Date/Time</td><td id='datetime_utc'>" + String(hasTime ? formatIsoUtc(utcNow) : "unsynced") + "</td></tr>";
  page += "<tr><td>Epoch</td><td id='epoch'>" + String(static_cast<uint32_t>(hasTime ? epochNow : 0)) + "</td></tr>";
  page += "<tr><td>Time Source</td><td id='time_source'>" + timeSourceName() + "</td></tr>";
  page += "</table>";
  page += "<form method='POST' action='/time'>";
  page += "<label>Manual Epoch (seconds UTC)</label><input name='epoch' type='number' min='1700000000' max='2208988800' placeholder='e.g. 1767225600'>";
  page += "<button type='submit'>Set Manual Time</button></form>";
  page += "<small>Use this if NTP is unavailable. Time is used for log metadata.</small></div>";

  page += "<div class='panel'><h2>Main System Parameters</h2><table>";
  page += "<tr><td>RPM</td><td id='rpm'>" + String((snapshot.rpm10 + 5U) / 10U) + "</td></tr>";
  page += "<tr><td>Advance (deg BTDC)</td><td id='advance_deg'>" + formatDeg10(snapshot.advanceDeg10) + "</td></tr>";
  page += "<tr><td>Crank Angle (deg)</td><td id='crank_deg'>" + formatDeg10(snapshot.crankAngleDeg10) + "</td></tr>";
  page += "<tr><td>Missing->TDC Offset (deg)</td><td id='missing_offset_deg'>" + formatDeg10(g_missingToothOffsetDeg10) + "</td></tr>";
  page += "<tr><td>Min Advance (deg)</td><td id='min_advance_deg'>" + formatDeg10(g_minAdvanceDeg10) + "</td></tr>";
  page += "<tr><td>Max Advance (deg)</td><td id='max_advance_deg'>" + formatDeg10(g_maxAdvanceDeg10) + "</td></tr>";
  page += "<tr><td>CDI Delay (us)</td><td id='cdi_delay_us'>" + String(g_cdiDelayUs) + "</td></tr>";
  page += "<tr><td>Dwell (us)</td><td id='dwell_us'>" + String(g_dwellUs) + "</td></tr>";
  page += "<tr><td>Strobe Enabled</td><td id='strobe_enabled'>" + String(g_strobeEnabled) + "</td></tr>";
  page += "<tr><td>Strobe Marker Mask</td><td id='strobe_mask'>" + String(g_strobeMarkerMask) + "</td></tr>";
  page += "<tr><td>Strobe Pulse (ms)</td><td id='strobe_pulse_ms'>" + String(g_strobePulseUs) + "</td></tr>";
  page += "<tr><td>Config Version</td><td id='config_version'>" + String(g_configVersion) + "</td></tr>";
  page += "</table></div>";

  page += "<div class='panel'><h2>Edit Key Parameters</h2>";
  page += "<form method='POST' action='/config'>";
  page += "<label>Missing->TDC Offset (deg10)</label><input name='missing_offset_deg10' type='number' min='-1800' max='1800' value='" + String(g_missingToothOffsetDeg10) + "' required>";
  page += "<label>Min Advance (deg10)</label><input name='min_advance_deg10' type='number' min='0' max='600' value='" + String(g_minAdvanceDeg10) + "' required>";
  page += "<label>Max Advance (deg10)</label><input name='max_advance_deg10' type='number' min='0' max='600' value='" + String(g_maxAdvanceDeg10) + "' required>";
  page += "<label>CDI Delay (us)</label><input name='cdi_delay_us' type='number' min='0' max='5000' value='" + String(g_cdiDelayUs) + "' required>";
  page += "<label>Dwell (us)</label><input name='dwell_us' type='number' min='100' max='20000' value='" + String(g_dwellUs) + "' required>";
  page += "<label>Strobe Enabled</label><select name='strobe_enabled'><option value='1'" + String(g_strobeEnabled ? " selected" : "") + ">1</option><option value='0'" + String(g_strobeEnabled ? "" : " selected") + ">0</option></select>";
  page += "<label>Strobe Mask (bitmask 0-7)</label><input name='strobe_mask' type='number' min='0' max='7' value='" + String(g_strobeMarkerMask) + "' required>";
  page += "<label>Strobe Pulse (ms)</label><input name='strobe_pulse_ms' type='number' min='1' max='50' value='" + String(g_strobePulseUs) + "' required>";
  page += "<button type='submit'>Apply Parameters</button></form>";
  page += "<small>Applied via logger queue; realtime task updates atomically at its next cycle.</small></div>";

  page += "<div class='panel'><h2>Boot WiFi Credentials</h2>";
  page += "<form method='POST' action='/wifi'>";
  page += "<label>SSID</label><input name='ssid' maxlength='32' required>";
  page += "<label>Password</label><input name='password' type='password' maxlength='63'>";
  page += "<button type='submit'>Save & Reboot</button></form>";
  page += "<small>Credentials are stored in NVS and applied on next boot.</small></div>";

  page +=
    "<script>"
    "async function refresh(){"
    "const r=await fetch('/api/status');"
    "if(!r.ok){return;}"
    "const d=await r.json();"
    "const keys=['wifi_mode','ip','datetime_utc','epoch','time_source','rpm','advance_deg','crank_deg','missing_offset_deg','min_advance_deg','max_advance_deg','cdi_delay_us','dwell_us','strobe_enabled','strobe_mask','strobe_pulse_ms','config_version'];"
    "for(const k of keys){const el=document.getElementById(k); if(el && d[k]!==undefined){el.textContent=d[k];}}"
    "}"
    "setInterval(refresh,1000);"
    "</script></body></html>";

  g_webServer.send(200, "text/html", page);
}

void handleStatusApi() {
  g_webServer.send(200, "application/json", buildStatusJson());
}

void handleWifiSave() {
  if (!g_webServer.hasArg("ssid")) {
    g_webServer.send(400, "text/plain", "Missing SSID");
    return;
  }

  String ssid = g_webServer.arg("ssid");
  String password = g_webServer.hasArg("password") ? g_webServer.arg("password") : "";
  ssid.trim();
  password.trim();

  if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 63) {
    g_webServer.send(400, "text/plain", "Invalid SSID or password length");
    return;
  }

  saveWifiCredentials(ssid, password);
  g_restartRequested = true;
  g_restartAtMs = millis() + WEB_RESTART_DELAY_MS;
  g_webServer.send(200, "text/html", "<html><body><h3>Credentials saved. Rebooting...</h3></body></html>");
}

void handleTimeSave() {
  if (!g_webServer.hasArg("epoch")) {
    g_webServer.send(400, "text/plain", "Missing epoch");
    return;
  }

  const int32_t epochValue = argToInt(g_webServer.arg("epoch"), 0);
  if (epochValue < static_cast<int32_t>(VALID_UNIX_EPOCH_MIN)) {
    g_webServer.send(400, "text/plain", "Epoch out of range");
    return;
  }

  timeval tv{};
  tv.tv_sec = epochValue;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  g_timeSetManually = true;
  g_timeSynced = true;
  g_timeSource = TIME_SOURCE_MANUAL;
  g_lastTimeSyncSuccessMs = millis();
  g_webServer.sendHeader("Location", "/");
  g_webServer.send(303, "text/plain", "");
}

void handleConfigSave() {
  LoggerConfig config{};
  config.version = static_cast<uint16_t>(g_configVersion + 1U);

  const int32_t missingOffset = argToInt(g_webServer.arg("missing_offset_deg10"), g_missingToothOffsetDeg10);
  const int32_t minAdvance = argToInt(g_webServer.arg("min_advance_deg10"), g_minAdvanceDeg10);
  const int32_t maxAdvance = argToInt(g_webServer.arg("max_advance_deg10"), g_maxAdvanceDeg10);
  const int32_t cdiDelay = argToInt(g_webServer.arg("cdi_delay_us"), g_cdiDelayUs);
  const int32_t dwellUs = argToInt(g_webServer.arg("dwell_us"), g_dwellUs);
  const int32_t strobeEnabled = argToInt(g_webServer.arg("strobe_enabled"), g_strobeEnabled);
  const int32_t strobeMask = argToInt(g_webServer.arg("strobe_mask"), g_strobeMarkerMask);
  const int32_t strobePulseMs = argToInt(g_webServer.arg("strobe_pulse_ms"), g_strobePulseUs);

  if (missingOffset < -1800 || missingOffset > 1800 ||
      minAdvance < 0 || minAdvance > 600 ||
      maxAdvance < 0 || maxAdvance > 600 ||
      minAdvance > maxAdvance ||
      cdiDelay < 0 || cdiDelay > 5000 ||
      dwellUs < 100 || dwellUs > 20000 ||
      (strobeEnabled != 0 && strobeEnabled != 1) ||
      strobeMask < 0 || strobeMask > STROBE_ALL ||
      strobePulseMs < 1 || strobePulseMs > 50) {
    g_webServer.send(400, "text/plain", "Invalid parameter range");
    return;
  }

  config.missingToothOffsetDeg10 = static_cast<int16_t>(missingOffset);
  config.minAdvanceDeg10 = static_cast<uint16_t>(minAdvance);
  config.maxAdvanceDeg10 = static_cast<uint16_t>(maxAdvance);
  config.cdiDelayUs = static_cast<uint16_t>(cdiDelay);
  config.dwellUs = static_cast<uint16_t>(dwellUs);
  config.strobeEnabled = static_cast<uint8_t>(strobeEnabled);
  config.strobeMarkerMask = static_cast<uint8_t>(strobeMask);
  config.strobePulseUs = static_cast<uint16_t>(strobePulseMs);

  xQueueOverwrite(configQueue, &config);
  g_webServer.sendHeader("Location", "/");
  g_webServer.send(303, "text/plain", "");
}

void startLoggerWebInterface() {
  String ssid;
  String password;
  loadStoredWifiCredentials(ssid, password);

  g_wifiConnected = connectToStationWifi(ssid, password);
  if (!g_wifiConnected) {
    startSetupPortal();
  } else {
    Serial.print("Connected to WiFi. IP: ");
    Serial.println(WiFi.localIP());
    syncTimeFromNtp();
  }

  g_webServer.on("/", HTTP_GET, handleRootPage);
  g_webServer.on("/api/status", HTTP_GET, handleStatusApi);
  g_webServer.on("/wifi", HTTP_POST, handleWifiSave);
  g_webServer.on("/time", HTTP_POST, handleTimeSave);
  g_webServer.on("/config", HTTP_POST, handleConfigSave);
  g_webServer.onNotFound([]() {
    g_webServer.sendHeader("Location", "/");
    g_webServer.send(302, "text/plain", "");
  });

  g_webServer.begin();
}

void loggerTask(void *param) {
  (void)param;
  TelemetrySnapshot snapshot{};
  const TickType_t telemetryPeriod = pdMS_TO_TICKS(200);
  TickType_t nextTelemetry = xTaskGetTickCount();
  uint8_t telemetrySeq = 0;
  startLoggerWebInterface();

  for (;;) {
    g_webServer.handleClient();
    maybeRefreshTimeSync();

    if (g_restartRequested && millis() >= g_restartAtMs) {
      ESP.restart();
    }

    LoggerConfig incomingConfig{};
    if (consumeConfigPacket(incomingConfig)) {
      xQueueOverwrite(configQueue, &incomingConfig);
      Serial.write("ACK\n");
    }

    if (xTaskGetTickCount() >= nextTelemetry) {
      if (xQueuePeek(telemetryQueue, &snapshot, 0) == pdTRUE) {
        sendTelemetryPacket(snapshot, telemetrySeq++);
      }
      nextTelemetry = xTaskGetTickCount() + telemetryPeriod;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

} // namespace

void startLoggerTask() {
  xTaskCreatePinnedToCore(loggerTask, "LoggerTask", 8192, nullptr, 1, nullptr, 1);
}
