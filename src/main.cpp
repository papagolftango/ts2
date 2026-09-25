#include <Arduino.h>

#include "ecu_shared.h"
#include "logger_core.h"
#include "realtime_core.h"

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ts2 ESP32 dual-core ignition starter");

  pinMode(TRIGGER_INPUT_PIN, INPUT_PULLUP);
  pinMode(IGNITION_OUTPUT_PIN, OUTPUT);
  pinMode(DEBUG_STROBE_PIN, OUTPUT);
  digitalWrite(IGNITION_OUTPUT_PIN, LOW);
  digitalWrite(DEBUG_STROBE_PIN, LOW);

  ignitionQueue = xQueueCreate(10, sizeof(IgnitionState));
  configQueue = xQueueCreate(1, sizeof(LoggerConfig));
  telemetryQueue = xQueueCreate(1, sizeof(TelemetrySnapshot));

  if (ignitionQueue == nullptr || configQueue == nullptr || telemetryQueue == nullptr) {
    Serial.println("Queue creation failed");
    while (true) {
      delay(100);
    }
  }

  initRealtimeCoreHardware();
  startRealtimeTask();
  startLoggerTask();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
