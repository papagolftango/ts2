#pragma once

#include <Arduino.h>

// Binary log format for logger-side storage/streaming.
// Little-endian, packed, checksum over payload bytes.

constexpr uint32_t LOG_MAGIC = 0x5453324c; // 'TS2L'
constexpr uint16_t LOG_FORMAT_VERSION = 1;

enum LogRecordType : uint8_t {
  LOG_REC_SESSION_HEADER = 0x01,
  LOG_REC_DICTIONARY_PARAM = 0x02,
  LOG_REC_SNAPSHOT = 0x03,
  LOG_REC_EVENT = 0x04,
  LOG_REC_VALUE = 0x05,
};

enum LogValueType : uint8_t {
  LOG_VAL_U8 = 1,
  LOG_VAL_I8 = 2,
  LOG_VAL_U16 = 3,
  LOG_VAL_I16 = 4,
  LOG_VAL_U32 = 5,
  LOG_VAL_I32 = 6,
  LOG_VAL_FLOAT32 = 7,
};

enum LogTimeSource : uint8_t {
  LOG_TIME_UNSYNCED = 0,
  LOG_TIME_NTP = 1,
  LOG_TIME_MANUAL = 2,
};

enum LogEventCode : uint16_t {
  LOG_EVT_ENGINE_START = 1,
  LOG_EVT_ENGINE_STOP = 2,
  LOG_EVT_CONFIG_APPLY = 3,
  LOG_EVT_TIME_SYNC = 4,
  LOG_EVT_SENSOR_FAULT = 5,
};

enum LogParamId : uint16_t {
  LOG_PARAM_RPM = 0x0001,
  LOG_PARAM_ADVANCE_DEG10 = 0x0002,
  LOG_PARAM_CRANK_DEG10 = 0x0003,
  LOG_PARAM_MISSING_OFFSET_DEG10 = 0x0004,
  LOG_PARAM_CDI_DELAY_US = 0x0005,
  LOG_PARAM_DWELL_US = 0x0006,
  LOG_PARAM_MIN_ADVANCE_DEG10 = 0x0007,
  LOG_PARAM_MAX_ADVANCE_DEG10 = 0x0008,
  LOG_PARAM_STROBE_ENABLED = 0x0009,
  LOG_PARAM_STROBE_MASK = 0x000A,
  LOG_PARAM_STROBE_PULSE_MS = 0x000B,
  LOG_PARAM_ENV_TEMP_C100 = 0x0101,
  LOG_PARAM_ENV_PRESSURE_HPA100 = 0x0102,
  LOG_PARAM_TIME_SOURCE = 0x0201,
  LOG_PARAM_EPOCH_UTC = 0x0202,
};

#pragma pack(push, 1)

struct LogFrameHeader {
  uint8_t recordType;
  uint16_t payloadLength;
  uint16_t seq;
  uint16_t crc16;
};

struct LogSessionHeaderRecord {
  uint32_t magic;
  uint16_t formatVersion;
  uint32_t sessionId;
  uint32_t bootCount;
  uint32_t startEpochUtc;
  uint8_t timeSource;
  uint32_t firmwareBuild;
  uint32_t deviceId;
};

struct LogDictionaryParamRecord {
  uint16_t paramId;
  uint8_t valueType;
  int32_t scaleNum;
  int32_t scaleDen;
  int32_t offset;
  uint8_t precision;
  uint8_t group;
  char name[24];
  char unit[12];
};

struct LogValueRecord {
  uint32_t dtMs;
  uint16_t paramId;
  uint8_t flags;
  int32_t valueRaw;
};

struct LogSnapshotRecord {
  uint32_t dtMs;
  uint16_t rpm;
  uint16_t advanceDeg10;
  uint16_t crankDeg10;
  int16_t envTempC100;
  uint32_t envPressureHpa100;
  uint8_t strobeEnabled;
  uint8_t strobeMask;
};

struct LogEventRecord {
  uint32_t dtMs;
  uint16_t eventCode;
  uint16_t arg0;
  uint32_t arg1;
};

#pragma pack(pop)
