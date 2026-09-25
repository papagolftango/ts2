# ts2 ESP32 ignition and logging platform

This project is a PlatformIO-based ESP32 application for a dual-core engine control architecture. The design separates the real-time ignition work from the telemetry/logging work so the timing-critical functions can stay deterministic while diagnostic data is streamed or stored without disturbing the ignition loop.

## Architecture

- Core 0: real-time engine timing and ignition control
- Core 1: logger and diagnostics task

The real-time side is intended to handle crank-angle timing, trigger-wheel edge capture, spark advance calculations, and eventually full ignition mapping. The logger side is expected to record RPM, angle, sensor health, and event data in a lower-priority task.

## Trigger wheel

The initial experiments use an optical trigger wheel. In practice, this means a 36:1 optical pattern such as a slotted or shuttered wheel producing 35 timing edges per engine revolution plus a missing-slot or reference gap. The code is structured so the wheel conventions can be adjusted as the exact sensor layout is confirmed.

The actual sensor being used for the first experiments is a DAOKAI IR slotted optocoupler module, operating from 3.3V to 5V and producing a digital switch output. These modules are typically beam-break devices: the output changes state when the IR beam is interrupted, and they are usually best read on the ESP32 using a pull-up input and an interrupt on the beam-break edge. The exact polarity can be inverted depending on the module wiring and beam orientation, so the code is set up to handle the practical break-beam case.

## Current scope

- ESP32 dual-core skeleton
- Trigger input edge detection on a dedicated GPIO
- RPM estimation from edge period
- queue-based communication from the real-time core to the logger core
- serial telemetry output for development and validation

## Planned evolution

1. Capture and validate raw trigger signal timing
2. Add crank angle and missing-tooth detection
3. Implement ignition dwell and advance calculation
4. Add spark channel timing and output driver support
5. Add mapped advance tables and temperature compensation
6. Add storage and dashboard logging for long sessions

## Workspace structure

- platformio.ini - PlatformIO configuration for the ESP32
- src/main.cpp - main application with both cores and the starter timing logic

## Getting started

From the project root:

```bash
pio run
pio device monitor
```


## Timing reference wheel

This degree-wheel view is the conceptual reference for the trigger-wheel timing model. It shows the fixed TDC reference, the missing-tooth reference marker, and the usable advance window from the minimum safe advance to the maximum tuned advance. The timing labels run anti-clockwise from TDC, while the engine itself rotates clockwise, so the ignition advance appears to the left of TDC on the wheel.

![Timing reference wheel](docs/timing-wheel.svg)

The aim is to eventually align the trigger wheel marks with this model so a strobe can highlight the expected TDC mark, the missing-tooth reference, and the spark advance arc while the engine is being calibrated.

## Notes

This is intentionally a starter project for an engine-control application. The real-time core should remain focused on timing-critical work, and any heavier processing such as logging, mapping, or data export should live on the non-real-time core or be queued asynchronously.
