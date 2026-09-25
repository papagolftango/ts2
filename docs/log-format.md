# ts2 binary log format

This file defines a compact logger-side format for ECU metadata and runtime values.

## Goals

- Low overhead on ESP32 logger task
- Stable IDs for parser compatibility
- Self-describing files with embedded dictionary
- Supports periodic snapshots and sparse events

## Record stream order

1. Session header record (once)
2. Dictionary parameter records (one per parameter, once)
3. Runtime records:
   - snapshot record at fixed interval (recommended: every 5 s)
   - value records for optional ad-hoc points
   - event records for state transitions/config/time sync

## Time model

- Use dtMs as monotonic delta from previous record
- Store initial absolute epoch in session header
- Include time source enum for trustability

## Suggested parameter IDs

- 0x0001: rpm (u16, scale 1)
- 0x0002: advance_deg10 (u16, scale 1/10 deg)
- 0x0003: crank_deg10 (u16, scale 1/10 deg)
- 0x0004: missing_offset_deg10 (i16, scale 1/10 deg)
- 0x0005: cdi_delay_us (u16, scale 1)
- 0x0006: dwell_us (u16, scale 1)
- 0x0101: env_temp_c100 (i16, scale 1/100 C)
- 0x0102: env_pressure_hpa100 (u32, scale 1/100 hPa)
- 0x0201: time_source (u8 enum)
- 0x0202: epoch_utc (u32)

## C structures

The canonical packed structure definitions are in [src/log_format.h](../src/log_format.h).
The editable parameter catalog used by the logger UI and validation is in [src/param_catalog.h](../src/param_catalog.h) and [src/param_catalog.cpp](../src/param_catalog.cpp), using the same parameter IDs.

## Recommended frequencies

- Read BMP280: 1 Hz
- Snapshot record: 0.2 Hz (every 5 s)
- Event record: on change (engine start/stop, config apply, time sync, sensor fault)
