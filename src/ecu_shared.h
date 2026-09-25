#pragma once

#include <Arduino.h>
#include <esp_timer.h>

constexpr uint8_t TRIGGER_INPUT_PIN = 35;
constexpr uint8_t IGNITION_OUTPUT_PIN = 4;
constexpr uint8_t DEBUG_STROBE_PIN = 25;
constexpr uint8_t TRIGGER_EDGES_PER_REV = 35;
constexpr uint32_t SAMPLE_PERIOD_MS = 10;
constexpr uint16_t MAX_RPM = 10000;
constexpr uint16_t CRANKING_RPM_THRESHOLD = 800;
constexpr uint16_t MIN_ADVANCE_TENTHS_DEG = 50;
constexpr uint8_t ADVANCE_TABLE_SIZE = 16;
constexpr uint32_t MAX_VALID_TOOTH_PERIOD_US = 250000;
constexpr uint32_t MISSING_TOOTH_GAP_FACTOR = 15;
constexpr uint32_t CDI_FIRE_DELAY_US = 250;
constexpr uint32_t IGNITION_DWELL_US = 2500;
constexpr uint32_t DEBUG_STROBE_PULSE_US = 5000;
constexpr int16_t MISSING_TOOTH_TO_TDC_OFFSET_DEG10 = 650;
constexpr uint16_t DEFAULT_MIN_ADVANCE_DEG10 = 50;
constexpr uint16_t DEFAULT_MAX_ADVANCE_DEG10 = 350;
constexpr uint16_t DEFAULT_CDI_DELAY_US = CDI_FIRE_DELAY_US;
constexpr uint16_t DEFAULT_DWELL_US = IGNITION_DWELL_US;

constexpr uint8_t PROTOCOL_START_BYTE = 0xA5;
constexpr uint8_t MSG_TYPE_CONFIG = 0x10;
constexpr uint8_t MSG_TYPE_TELEMETRY = 0x20;

enum StrobeMarker : uint8_t {
  STROBE_NONE = 0,
  STROBE_TDC = 1 << 0,
  STROBE_MISSING_TOOTH = 1 << 1,
  STROBE_CURRENT_ADVANCE = 1 << 2,
  STROBE_ALL = STROBE_TDC | STROBE_MISSING_TOOTH | STROBE_CURRENT_ADVANCE,
};

struct IgnitionState {
  uint32_t edgeCount = 0;
  uint32_t lastEdgeUs = 0;
  uint32_t periodUs = 0;
  uint32_t rpm10 = 0;
  uint16_t crankAngleDeg10 = 0;
  uint16_t sparkAdvanceDeg10 = 0;
  uint16_t dwellMs10 = 0;
};

struct LoggerConfig {
  uint16_t version = 0;
  int16_t missingToothOffsetDeg10 = MISSING_TOOTH_TO_TDC_OFFSET_DEG10;
  uint16_t minAdvanceDeg10 = DEFAULT_MIN_ADVANCE_DEG10;
  uint16_t maxAdvanceDeg10 = DEFAULT_MAX_ADVANCE_DEG10;
  uint16_t cdiDelayUs = DEFAULT_CDI_DELAY_US;
  uint16_t dwellUs = DEFAULT_DWELL_US;
  uint8_t strobeMarkerMask = STROBE_TDC;
  uint8_t strobeEnabled = 1;
  uint16_t strobePulseUs = static_cast<uint16_t>(DEBUG_STROBE_PULSE_US / 1000U);
};

struct TelemetrySnapshot {
  uint32_t timeUs = 0;
  uint16_t rpm10 = 0;
  uint16_t advanceDeg10 = 0;
  uint16_t crankAngleDeg10 = 0;
  uint8_t flags = 0;
};

#pragma pack(push, 1)
struct ConfigPacket {
  uint8_t start;
  uint8_t type;
  uint8_t seq;
  uint8_t length;
  uint16_t version;
  int16_t missingToothOffsetDeg10;
  uint16_t minAdvanceDeg10;
  uint16_t maxAdvanceDeg10;
  uint16_t cdiDelayUs;
  uint16_t dwellUs;
  uint8_t strobeMarkerMask;
  uint8_t strobeEnabled;
  uint16_t strobePulseUs;
  uint8_t checksum;
};

struct TelemetryPacket {
  uint8_t start;
  uint8_t type;
  uint8_t seq;
  uint8_t length;
  uint32_t timeUs;
  uint16_t rpm10;
  uint16_t advanceDeg10;
  uint16_t crankAngleDeg10;
  uint8_t flags;
  uint8_t checksum;
};
#pragma pack(pop)

extern volatile uint32_t g_lastEdgeUs;
extern volatile uint32_t g_periodUs;
extern volatile uint32_t g_edgeCount;
extern volatile bool g_missingToothDetected;
extern volatile uint32_t g_lastGoodPeriodUs;
extern volatile uint32_t g_sparkDueUs;
extern volatile bool g_sparkPending;
extern volatile int16_t g_missingToothOffsetDeg10;
extern volatile uint16_t g_minAdvanceDeg10;
extern volatile uint16_t g_maxAdvanceDeg10;
extern volatile uint16_t g_cdiDelayUs;
extern volatile uint16_t g_dwellUs;
extern volatile uint8_t g_strobeMarkerMask;
extern volatile uint8_t g_strobeEnabled;
extern volatile uint16_t g_strobePulseUs;
extern volatile uint32_t g_configVersion;
extern esp_timer_handle_t g_strobeOffTimer;

extern QueueHandle_t ignitionQueue;
extern QueueHandle_t configQueue;
extern QueueHandle_t telemetryQueue;

uint32_t estimateRpmTenths(uint32_t periodUs);
uint16_t lookupAdvanceTenthsDeg(uint16_t rpm);
int32_t computeSparkTimeUs(uint32_t revPeriodUs, uint16_t advanceDeg10, int16_t refOffsetDeg10);
void scheduleNextIgnitionEvent(uint32_t revPeriodUs, uint16_t advanceDeg10, int16_t missingToothOffsetDeg10);

uint8_t calcChecksum8(const uint8_t *buffer, size_t len);
void sendConfigPacket(const LoggerConfig &config, uint8_t seq);
void sendTelemetryPacket(const TelemetrySnapshot &snapshot, uint8_t seq);
bool consumeConfigPacket(LoggerConfig &configOut);
