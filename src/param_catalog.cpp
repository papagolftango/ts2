#include "param_catalog.h"

namespace {

const EditableParamSpec kEditableParamSpecs[] = {
  { LOG_PARAM_MISSING_OFFSET_DEG10, "missing_offset_deg10", "Missing->TDC Offset", "deg10", -1800, 1800, EDIT_FIELD_SIGNED },
  { LOG_PARAM_MIN_ADVANCE_DEG10, "min_advance_deg10", "Min Advance", "deg10", 0, 600, EDIT_FIELD_UNSIGNED },
  { LOG_PARAM_MAX_ADVANCE_DEG10, "max_advance_deg10", "Max Advance", "deg10", 0, 600, EDIT_FIELD_UNSIGNED },
  { LOG_PARAM_CDI_DELAY_US, "cdi_delay_us", "CDI Delay", "us", 0, 5000, EDIT_FIELD_UNSIGNED },
  { LOG_PARAM_DWELL_US, "dwell_us", "Dwell", "us", 100, 20000, EDIT_FIELD_UNSIGNED },
  { LOG_PARAM_STROBE_ENABLED, "strobe_enabled", "Strobe Enabled", "bool", 0, 1, EDIT_FIELD_BOOL },
  { LOG_PARAM_STROBE_MASK, "strobe_mask", "Strobe Mask", "bitmask", 0, 7, EDIT_FIELD_UNSIGNED },
  { LOG_PARAM_STROBE_PULSE_MS, "strobe_pulse_ms", "Strobe Pulse", "ms", 1, 50, EDIT_FIELD_UNSIGNED },
};

} // namespace

const EditableParamSpec *getEditableParamSpecs(size_t &count) {
  count = sizeof(kEditableParamSpecs) / sizeof(kEditableParamSpecs[0]);
  return kEditableParamSpecs;
}
