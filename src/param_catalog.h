#pragma once

#include <Arduino.h>

#include "log_format.h"

enum EditableFieldType : uint8_t {
  EDIT_FIELD_SIGNED = 0,
  EDIT_FIELD_UNSIGNED = 1,
  EDIT_FIELD_BOOL = 2,
};

struct EditableParamSpec {
  uint16_t paramId;
  const char *fieldName;
  const char *label;
  const char *unit;
  int32_t minValue;
  int32_t maxValue;
  EditableFieldType fieldType;
};

const EditableParamSpec *getEditableParamSpecs(size_t &count);
