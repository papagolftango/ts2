#include "realtime_core.h"

#include "ecu_shared.h"

namespace {

void strobeOffCallback(void *arg) {
  (void)arg;
  digitalWrite(DEBUG_STROBE_PIN, LOW);
}

void fireStrobeMarker(uint8_t markerMask, uint16_t crankAngleDeg10, uint16_t advanceDeg10) {
  if (g_strobeEnabled == 0 || g_strobeMarkerMask == STROBE_NONE) {
    return;
  }

  const uint8_t activeMask = g_strobeMarkerMask & markerMask;
  if (activeMask == 0) {
    return;
  }

  digitalWrite(DEBUG_STROBE_PIN, HIGH);
  if (g_strobeOffTimer != nullptr) {
    esp_timer_stop(g_strobeOffTimer);
    esp_timer_start_once(g_strobeOffTimer, static_cast<uint64_t>(g_strobePulseUs) * 1000ULL);
  }
  (void)crankAngleDeg10;
  (void)advanceDeg10;
}

void triggerIgnitionPulse() {
  if (g_sparkPending == false) {
    return;
  }

  digitalWrite(IGNITION_OUTPUT_PIN, HIGH);
  delayMicroseconds(g_dwellUs);
  digitalWrite(IGNITION_OUTPUT_PIN, LOW);
  g_sparkPending = false;
}

void IRAM_ATTR onTriggerEdge() {
  const uint32_t now = micros();

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
  (void)param;
  TickType_t lastWake = xTaskGetTickCount();
  IgnitionState state{};
  LoggerConfig config{};
  uint8_t configSeq = 0;

  for (;;) {
    while (xQueueReceive(configQueue, &config, 0) == pdTRUE) {
      g_missingToothOffsetDeg10 = config.missingToothOffsetDeg10;
      g_minAdvanceDeg10 = config.minAdvanceDeg10;
      g_maxAdvanceDeg10 = config.maxAdvanceDeg10;
      g_cdiDelayUs = config.cdiDelayUs;
      g_dwellUs = config.dwellUs;
      g_strobeMarkerMask = config.strobeMarkerMask;
      g_strobeEnabled = config.strobeEnabled;
      g_strobePulseUs = config.strobePulseUs;
      g_configVersion = config.version;
      sendConfigPacket(config, configSeq++);
    }

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
      state.dwellMs10 = static_cast<uint16_t>((200U + (rpm * 50U)) / 10U);

      if (g_missingToothDetected) {
        const int32_t adjustedSparkUs = computeSparkTimeUs(g_periodUs, sparkAdvanceDeg10, g_missingToothOffsetDeg10);
        fireStrobeMarker(STROBE_MISSING_TOOTH, crankAngleDeg10, sparkAdvanceDeg10);
        scheduleNextIgnitionEvent(g_periodUs, sparkAdvanceDeg10, g_missingToothOffsetDeg10);
        g_missingToothDetected = false;
        (void)adjustedSparkUs;
      }

      if (g_sparkPending && micros() >= g_sparkDueUs) {
        fireStrobeMarker(STROBE_CURRENT_ADVANCE, crankAngleDeg10, sparkAdvanceDeg10);
        triggerIgnitionPulse();
      }

      if (g_strobeEnabled && (g_strobeMarkerMask & STROBE_TDC) != 0 && (g_edgeCount % TRIGGER_EDGES_PER_REV) == 0) {
        fireStrobeMarker(STROBE_TDC, crankAngleDeg10, sparkAdvanceDeg10);
      }

      xQueueSend(ignitionQueue, &state, 0);

      TelemetrySnapshot snapshot{};
      snapshot.timeUs = micros();
      snapshot.rpm10 = static_cast<uint16_t>(rpm10);
      snapshot.advanceDeg10 = sparkAdvanceDeg10;
      snapshot.crankAngleDeg10 = crankAngleDeg10;
      snapshot.flags = g_missingToothDetected ? 1U : 0U;
      xQueueOverwrite(telemetryQueue, &snapshot);
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
  }
}

} // namespace

void initRealtimeCoreHardware() {
  esp_timer_create_args_t strobeArgs{};
  strobeArgs.callback = strobeOffCallback;
  strobeArgs.arg = nullptr;
  strobeArgs.name = "strobe_off";
  esp_timer_create(&strobeArgs, &g_strobeOffTimer);

  attachInterrupt(digitalPinToInterrupt(TRIGGER_INPUT_PIN), onTriggerEdge, CHANGE);
}

void startRealtimeTask() {
  xTaskCreatePinnedToCore(realtimeTask, "RealtimeIgnition", 4096, nullptr, 3, nullptr, 0);
}
