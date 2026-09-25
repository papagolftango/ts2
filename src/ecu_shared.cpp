#include "ecu_shared.h"

static const uint16_t kAdvanceTable[ADVANCE_TABLE_SIZE] = {
  50, 60, 80, 100, 130, 160, 190, 220,
  250, 280, 300, 320, 335, 345, 350, 350
};

volatile uint32_t g_lastEdgeUs = 0;
volatile uint32_t g_periodUs = 0;
volatile uint32_t g_edgeCount = 0;
volatile bool g_missingToothDetected = false;
volatile uint32_t g_lastGoodPeriodUs = 0;
volatile uint32_t g_sparkDueUs = 0;
volatile bool g_sparkPending = false;
volatile int16_t g_missingToothOffsetDeg10 = MISSING_TOOTH_TO_TDC_OFFSET_DEG10;
volatile uint16_t g_minAdvanceDeg10 = DEFAULT_MIN_ADVANCE_DEG10;
volatile uint16_t g_maxAdvanceDeg10 = DEFAULT_MAX_ADVANCE_DEG10;
volatile uint16_t g_cdiDelayUs = DEFAULT_CDI_DELAY_US;
volatile uint16_t g_dwellUs = DEFAULT_DWELL_US;
volatile uint8_t g_strobeMarkerMask = STROBE_TDC;
volatile uint8_t g_strobeEnabled = 1;
volatile uint16_t g_strobePulseUs = static_cast<uint16_t>(DEBUG_STROBE_PULSE_US / 1000U);
volatile uint32_t g_configVersion = 0;
esp_timer_handle_t g_strobeOffTimer = nullptr;

QueueHandle_t ignitionQueue = nullptr;
QueueHandle_t configQueue = nullptr;
QueueHandle_t telemetryQueue = nullptr;

uint32_t estimateRpmTenths(uint32_t periodUs) {
  if (periodUs == 0) {
    return 0;
  }

  return static_cast<uint32_t>((60000000ULL * 10ULL) / (static_cast<uint64_t>(periodUs) * TRIGGER_EDGES_PER_REV));
}

uint16_t lookupAdvanceTenthsDeg(uint16_t rpm) {
  if (rpm < CRANKING_RPM_THRESHOLD) {
    return g_minAdvanceDeg10;
  }

  if (rpm >= MAX_RPM) {
    return g_maxAdvanceDeg10;
  }

  const uint32_t tableIndex = (static_cast<uint32_t>(rpm) * (ADVANCE_TABLE_SIZE - 1U)) / MAX_RPM;
  const uint16_t index = static_cast<uint16_t>(tableIndex);
  const uint16_t nextIndex = (index + 1U < ADVANCE_TABLE_SIZE) ? (index + 1U) : (ADVANCE_TABLE_SIZE - 1U);

  const uint16_t lowAdvance = kAdvanceTable[index];
  const uint16_t highAdvance = kAdvanceTable[nextIndex];

  const uint32_t rpmLow = (static_cast<uint32_t>(index) * MAX_RPM) / (ADVANCE_TABLE_SIZE - 1U);
  const uint32_t rpmHigh = (static_cast<uint32_t>(nextIndex) * MAX_RPM) / (ADVANCE_TABLE_SIZE - 1U);

  if (rpmHigh == rpmLow) {
    return lowAdvance;
  }

  const uint32_t fraction = ((rpm - rpmLow) << 16U) / (rpmHigh - rpmLow);
  const uint32_t interpolated = static_cast<uint32_t>(lowAdvance) +
                               (((static_cast<uint32_t>(highAdvance - lowAdvance)) * fraction) >> 16U);

  return static_cast<uint16_t>(interpolated);
}

int32_t computeSparkTimeUs(uint32_t revPeriodUs, uint16_t advanceDeg10, int16_t refOffsetDeg10) {
  const uint32_t degreesPerRev = 3600U;
  const uint32_t sparkAngleFromRef = static_cast<uint32_t>(advanceDeg10 + refOffsetDeg10);
  const uint32_t sparkFraction = (sparkAngleFromRef * revPeriodUs) / degreesPerRev;
  return static_cast<int32_t>(revPeriodUs - sparkFraction - g_cdiDelayUs);
}

void scheduleNextIgnitionEvent(uint32_t revPeriodUs, uint16_t advanceDeg10, int16_t missingToothOffsetDeg10) {
  const uint32_t degreesPerRev = 3600U;
  const uint32_t advanceFromReferenceUs =
    (static_cast<uint32_t>(advanceDeg10 + missingToothOffsetDeg10) * revPeriodUs) / degreesPerRev;
  const uint32_t nowUs = micros();
  const uint32_t fireWindowUs = (advanceFromReferenceUs > g_cdiDelayUs) ? (advanceFromReferenceUs - g_cdiDelayUs) : 0U;

  g_sparkDueUs = nowUs + fireWindowUs;
  g_sparkPending = true;
}

uint8_t calcChecksum8(const uint8_t *buffer, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; ++i) {
    sum += buffer[i];
  }
  return sum;
}

void sendConfigPacket(const LoggerConfig &config, uint8_t seq) {
  ConfigPacket packet{};
  packet.start = PROTOCOL_START_BYTE;
  packet.type = MSG_TYPE_CONFIG;
  packet.seq = seq;
  packet.length = sizeof(ConfigPacket) - 4U;
  packet.version = config.version;
  packet.missingToothOffsetDeg10 = config.missingToothOffsetDeg10;
  packet.minAdvanceDeg10 = config.minAdvanceDeg10;
  packet.maxAdvanceDeg10 = config.maxAdvanceDeg10;
  packet.cdiDelayUs = config.cdiDelayUs;
  packet.dwellUs = config.dwellUs;
  packet.strobeMarkerMask = config.strobeMarkerMask;
  packet.strobeEnabled = config.strobeEnabled;
  packet.strobePulseUs = config.strobePulseUs;

  uint8_t bytes[sizeof(ConfigPacket)];
  memcpy(bytes, &packet, sizeof(packet));
  packet.checksum = calcChecksum8(bytes + 1, sizeof(ConfigPacket) - 2);
  Serial.write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
}

void sendTelemetryPacket(const TelemetrySnapshot &snapshot, uint8_t seq) {
  TelemetryPacket packet{};
  packet.start = PROTOCOL_START_BYTE;
  packet.type = MSG_TYPE_TELEMETRY;
  packet.seq = seq;
  packet.length = 9;
  packet.timeUs = snapshot.timeUs;
  packet.rpm10 = snapshot.rpm10;
  packet.advanceDeg10 = snapshot.advanceDeg10;
  packet.crankAngleDeg10 = snapshot.crankAngleDeg10;
  packet.flags = snapshot.flags;

  uint8_t bytes[sizeof(TelemetryPacket)];
  memcpy(bytes, &packet, sizeof(packet));
  packet.checksum = calcChecksum8(bytes + 1, sizeof(TelemetryPacket) - 2);
  Serial.write(reinterpret_cast<const uint8_t *>(&packet), sizeof(packet));
}

bool consumeConfigPacket(LoggerConfig &configOut) {
  if (Serial.available() < static_cast<int>(sizeof(ConfigPacket))) {
    return false;
  }

  uint8_t bytes[sizeof(ConfigPacket)];
  Serial.readBytes(reinterpret_cast<char *>(bytes), sizeof(bytes));

  if (bytes[0] != PROTOCOL_START_BYTE) {
    return false;
  }

  if (bytes[1] != MSG_TYPE_CONFIG) {
    return false;
  }

  const uint8_t expectedChecksum = calcChecksum8(bytes + 1, sizeof(ConfigPacket) - 2);
  if (bytes[sizeof(ConfigPacket) - 1] != expectedChecksum) {
    return false;
  }

  ConfigPacket packet{};
  memcpy(&packet, bytes, sizeof(packet));
  configOut.version = packet.version;
  configOut.missingToothOffsetDeg10 = packet.missingToothOffsetDeg10;
  configOut.minAdvanceDeg10 = packet.minAdvanceDeg10;
  configOut.maxAdvanceDeg10 = packet.maxAdvanceDeg10;
  configOut.cdiDelayUs = packet.cdiDelayUs;
  configOut.dwellUs = packet.dwellUs;
  configOut.strobeMarkerMask = packet.strobeMarkerMask;
  configOut.strobeEnabled = packet.strobeEnabled;
  configOut.strobePulseUs = packet.strobePulseUs;
  return true;
}
