#include <Arduino.h>

constexpr uint8_t TRIGGER_INPUT_PIN = 35;
constexpr uint8_t IGNITION_OUTPUT_PIN = 4;
constexpr uint8_t TRIGGER_EDGES_PER_REV = 35; // Optical 36:1 wheel convention: 35 slot edges + 1 missing reference gap
constexpr uint32_t SAMPLE_PERIOD_MS = 10;
constexpr uint16_t MAX_RPM = 10000;
constexpr uint16_t CRANKING_RPM_THRESHOLD = 800;
constexpr uint16_t MIN_ADVANCE_TENTHS_DEG = 50; // 5.0° BTDC safe fallback for cranking and low-RPM operation
constexpr uint8_t ADVANCE_TABLE_SIZE = 16;
constexpr uint32_t MAX_VALID_TOOTH_PERIOD_US = 250000;
constexpr uint32_t MISSING_TOOTH_GAP_FACTOR = 15; // 1.5x normal tooth period threshold
constexpr uint32_t CDI_FIRE_DELAY_US = 250; // calibrated delay from fire edge to actual spark output
constexpr uint32_t IGNITION_DWELL_US = 2500; // 2.5 ms active-high dwell for the coil/CDI trigger
constexpr int16_t MISSING_TOOTH_TO_TDC_OFFSET_DEG10 = 650; // 65.0° reference offset; missing tooth occurs before TDC and is setup-calibrated

struct IgnitionState {
  uint32_t edgeCount = 0;
  uint32_t lastEdgeUs = 0;
  uint32_t periodUs = 0;
  uint32_t rpm10 = 0;
  uint16_t crankAngleDeg10 = 0;
  uint16_t sparkAdvanceDeg10 = 0;
  uint16_t dwellMs10 = 0;
};

// BTDC angle lookup in tenths of a degree.
// This is a safe cranking/low-RPM baseline at 5° BTDC, with advance increasing with RPM
// and reaching a larger final value at max engine speed. The exact optimum curve must be
// calibrated on the engine, but the direction must be monotonic upward as revs rise.
static const uint16_t kAdvanceTable[ADVANCE_TABLE_SIZE] = {
  50, 60, 80, 100, 130, 160, 190, 220,
  250, 280, 300, 320, 335, 345, 350, 350
};

static volatile uint32_t g_lastEdgeUs = 0;
static volatile uint32_t g_periodUs = 0;
static volatile uint32_t g_edgeCount = 0;
static volatile bool g_missingToothDetected = false;
static volatile uint32_t g_lastGoodPeriodUs = 0;
static volatile uint32_t g_sparkDueUs = 0;
static volatile bool g_sparkPending = false;
static QueueHandle_t ignitionQueue = nullptr;

uint32_t estimateRpmTenths(uint32_t periodUs) {
  if (periodUs == 0) {
    return 0;
  }

  // RPM = 60 / (periodUs * edgesPerRev / 1e6)
  // Keep the result in tenths of RPM to avoid float math in the timing path.
  return static_cast<uint32_t>((60000000ULL * 10ULL) / (static_cast<uint64_t>(periodUs) * TRIGGER_EDGES_PER_REV));
}

uint16_t lookupAdvanceTenthsDeg(uint16_t rpm) {
  if (rpm < CRANKING_RPM_THRESHOLD) {
    return MIN_ADVANCE_TENTHS_DEG;
  }

  if (rpm >= MAX_RPM) {
    return kAdvanceTable[ADVANCE_TABLE_SIZE - 1];
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
  return static_cast<int32_t>(revPeriodUs - sparkFraction - CDI_FIRE_DELAY_US);
}

void triggerIgnitionPulse() {
  if (g_sparkPending == false) {
    return;
  }

  digitalWrite(IGNITION_OUTPUT_PIN, HIGH);
  delayMicroseconds(IGNITION_DWELL_US);
  digitalWrite(IGNITION_OUTPUT_PIN, LOW);
  g_sparkPending = false;
}

void scheduleNextIgnitionEvent(uint32_t revPeriodUs, uint16_t advanceDeg10) {
  const uint32_t degreesPerRev = 3600U;
  const uint32_t advanceFromReferenceUs = (static_cast<uint32_t>(advanceDeg10 + MISSING_TOOTH_TO_TDC_OFFSET_DEG10) * revPeriodUs) / degreesPerRev;
  const uint32_t nowUs = micros();
  const uint32_t fireWindowUs = (advanceFromReferenceUs > CDI_FIRE_DELAY_US) ? (advanceFromReferenceUs - CDI_FIRE_DELAY_US) : 0U;

  g_sparkDueUs = nowUs + fireWindowUs;
  g_sparkPending = true;
}

void IRAM_ATTR onTriggerEdge() {
  const uint32_t now = micros();

  // For a 36:1 optical wheel, normal tooth spacing is approximately constant.
  // The missing-tooth gap is significantly larger than a normal slot spacing,
  // so we detect a reference event by the pulse-to-pulse period growing beyond the
  // normal tooth period multiplied by a margin.
  if (digitalRead(TRIGGER_INPUT_PIN) == LOW) {
    if (g_lastEdgeUs != 0) {
      const uint32_t delta = now - g_lastEdgeUs;
      if (delta > 0 && delta < MAX_VALID_TOOTH_PERIOD_US) {
        if (g_lastGoodPeriodUs != 0) {
          const uint32_t missingToothThreshold = (g_lastGoodPeriodUs * MISSING_TOOTH_GAP_FACTOR) / 10U;
          if (delta > missingToothThreshold) {
            g_missingToothDetected = true;
          }
        }

        g_lastGoodPeriodUs = delta;
        g_periodUs = delta;
      }
    }

    g_lastEdgeUs = now;
    g_edgeCount++;
  }
}

void realtimeTask(void *param) {
  TickType_t lastWake = xTaskGetTickCount();
  IgnitionState state{};

  for (;;) {
    if (g_periodUs > 0) {
      const uint32_t rpm10 = estimateRpmTenths(g_periodUs);
      const uint16_t rpm = static_cast<uint16_t>((rpm10 + 5U) / 10U);
      const uint16_t sparkAdvanceDeg10 = lookupAdvanceTenthsDeg(rpm);
      const uint16_t crankAngleDeg10 = static_cast<uint16_t>((
        (static_cast<uint32_t>(g_edgeCount % TRIGGER_EDGES_PER_REV) * 3600U) /
        static_cast<uint32_t>(TRIGGER_EDGES_PER_REV)));

      state.edgeCount = g_edgeCount;
      state.lastEdgeUs = g_lastEdgeUs;
      state.periodUs = g_periodUs;
      state.rpm10 = rpm10;
      state.crankAngleDeg10 = crankAngleDeg10;
      state.sparkAdvanceDeg10 = sparkAdvanceDeg10;
      state.dwellMs10 = static_cast<uint16_t>((200U + (rpm * 50U)) / 10U); // rough 2.0ms + rpm-dependent dwell in 0.1ms units

      if (g_missingToothDetected) {
        const int32_t adjustedSparkUs = computeSparkTimeUs(g_periodUs, sparkAdvanceDeg10, MISSING_TOOTH_TO_TDC_OFFSET_DEG10);
        Serial.printf("missing tooth detected; spark window=%ld us with cdi delay=%lu us\n",
                      adjustedSparkUs,
                      static_cast<unsigned long>(CDI_FIRE_DELAY_US));
        scheduleNextIgnitionEvent(g_periodUs, sparkAdvanceDeg10);
        g_missingToothDetected = false;
      }

      if (g_sparkPending && micros() >= g_sparkDueUs) {
        triggerIgnitionPulse();
      }

      xQueueSend(ignitionQueue, &state, 0);
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  }
}

void loggerTask(void *param) {
  IgnitionState state{};

  for (;;) {
    if (xQueueReceive(ignitionQueue, &state, portMAX_DELAY) == pdTRUE) {
      const float rpm = state.rpm10 / 10.0f;
      const float crankAngle = state.crankAngleDeg10 / 10.0f;
      const float sparkAdvance = state.sparkAdvanceDeg10 / 10.0f;
      const float dwellMs = state.dwellMs10 / 10.0f;

      Serial.printf(
        "rpm=%.1f crankAngle=%.1f sparkAdv=%.1f dwell=%.2fms edges=%lu\n",
        rpm,
        crankAngle,
        sparkAdvance,
        dwellMs,
        state.edgeCount);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ts2 ESP32 dual-core ignition starter");

  pinMode(TRIGGER_INPUT_PIN, INPUT_PULLUP);
  pinMode(IGNITION_OUTPUT_PIN, OUTPUT);
  digitalWrite(IGNITION_OUTPUT_PIN, LOW);
  // These IR optocoupler modules are usually beam-break sensors; the exact edge polarity
  // depends on wiring, so we trigger on the beam-interrupted state and monitor it in code.
  attachInterrupt(digitalPinToInterrupt(TRIGGER_INPUT_PIN), onTriggerEdge, CHANGE);

  ignitionQueue = xQueueCreate(10, sizeof(IgnitionState));
  if (ignitionQueue == nullptr) {
    Serial.println("Queue creation failed");
    while (true) {
      delay(100);
    }
  }

  xTaskCreatePinnedToCore(realtimeTask, "RealtimeIgnition", 4096, nullptr, 3, nullptr, 0);
  xTaskCreatePinnedToCore(loggerTask, "LoggerTask", 4096, nullptr, 1, nullptr, 1);
}

void loop() {
  // The timing-critical work lives on Core 0, while the logger sits on Core 1.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
