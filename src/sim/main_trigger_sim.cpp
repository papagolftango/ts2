#include <Arduino.h>

namespace {

constexpr uint8_t TRIGGER_OUT_PIN = 26;
constexpr uint8_t ENCODER_A_PIN = 32;
constexpr uint8_t ENCODER_B_PIN = 33;

constexpr uint8_t WHEEL_SLOTS = 36;
constexpr uint8_t MISSING_TOOTH_SLOT = 35;

constexpr uint16_t RPM_MIN = 300;
constexpr uint16_t RPM_MAX = 10000;
constexpr uint16_t RPM_STEP = 50;
constexpr uint16_t DEFAULT_RPM = 1000;

constexpr uint16_t TOOTH_PULSE_LOW_US = 40;
constexpr uint16_t MIN_SLOT_TIME_US = 80;

uint16_t g_targetRpm = DEFAULT_RPM;
uint8_t g_slotIndex = 0;
uint8_t g_lastEncoderState = 0;
int16_t g_encoderAccum = 0;
uint32_t g_lastStatusPrintMs = 0;

int8_t decodeQuadratureStep(uint8_t previousState, uint8_t currentState) {
  static const int8_t kDecodeLut[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
  };

  const uint8_t lutIndex = static_cast<uint8_t>((previousState << 2U) | currentState);
  return kDecodeLut[lutIndex];
}

void printStatusIfDue() {
  const uint32_t nowMs = millis();
  if ((nowMs - g_lastStatusPrintMs) < 500U) {
    return;
  }

  g_lastStatusPrintMs = nowMs;
  Serial.print("Trigger sim RPM: ");
  Serial.print(g_targetRpm);
  Serial.print(" slot_us: ");
  uint32_t slotUs = (60000000UL / static_cast<uint32_t>(g_targetRpm)) / WHEEL_SLOTS;
  if (slotUs < MIN_SLOT_TIME_US) {
    slotUs = MIN_SLOT_TIME_US;
  }
  Serial.println(slotUs);
}

void applyEncoderStep(int8_t direction) {
  if (direction == 0) {
    return;
  }

  const int32_t nextRpm = static_cast<int32_t>(g_targetRpm) + (static_cast<int32_t>(direction) * RPM_STEP);
  if (nextRpm < RPM_MIN) {
    g_targetRpm = RPM_MIN;
  } else if (nextRpm > RPM_MAX) {
    g_targetRpm = RPM_MAX;
  } else {
    g_targetRpm = static_cast<uint16_t>(nextRpm);
  }
}

void pollEncoder() {
  const uint8_t currentState = static_cast<uint8_t>((digitalRead(ENCODER_A_PIN) << 1U) | digitalRead(ENCODER_B_PIN));
  const int8_t step = decodeQuadratureStep(g_lastEncoderState, currentState);
  g_lastEncoderState = currentState;

  if (step == 0) {
    return;
  }

  g_encoderAccum += step;
  while (g_encoderAccum >= 4) {
    applyEncoderStep(1);
    g_encoderAccum -= 4;
  }
  while (g_encoderAccum <= -4) {
    applyEncoderStep(-1);
    g_encoderAccum += 4;
  }
}

void waitWithEncoderPolling(uint32_t delayUs) {
  const uint32_t startUs = micros();
  while ((micros() - startUs) < delayUs) {
    pollEncoder();
  }
}

void outputTriggerSlot() {
  uint32_t slotUs = (60000000UL / static_cast<uint32_t>(g_targetRpm)) / WHEEL_SLOTS;
  if (slotUs < MIN_SLOT_TIME_US) {
    slotUs = MIN_SLOT_TIME_US;
  }

  if (g_slotIndex == MISSING_TOOTH_SLOT) {
    waitWithEncoderPolling(slotUs);
  } else {
    const uint32_t pulseLowUs = (slotUs > TOOTH_PULSE_LOW_US) ? TOOTH_PULSE_LOW_US : (slotUs / 2U);
    digitalWrite(TRIGGER_OUT_PIN, LOW);
    waitWithEncoderPolling(pulseLowUs);
    digitalWrite(TRIGGER_OUT_PIN, HIGH);
    waitWithEncoderPolling(slotUs - pulseLowUs);
  }

  g_slotIndex = static_cast<uint8_t>((g_slotIndex + 1U) % WHEEL_SLOTS);
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("ts2 trigger simulator start");

  pinMode(TRIGGER_OUT_PIN, OUTPUT);
  digitalWrite(TRIGGER_OUT_PIN, HIGH);

  pinMode(ENCODER_A_PIN, INPUT_PULLUP);
  pinMode(ENCODER_B_PIN, INPUT_PULLUP);
  g_lastEncoderState = static_cast<uint8_t>((digitalRead(ENCODER_A_PIN) << 1U) | digitalRead(ENCODER_B_PIN));

  Serial.print("Trigger GPIO: ");
  Serial.println(TRIGGER_OUT_PIN);
  Serial.print("Encoder A/B GPIO: ");
  Serial.print(ENCODER_A_PIN);
  Serial.print("/");
  Serial.println(ENCODER_B_PIN);
  Serial.print("RPM range: ");
  Serial.print(RPM_MIN);
  Serial.print("..");
  Serial.println(RPM_MAX);
}

void loop() {
  outputTriggerSlot();
  printStatusIfDue();
}
