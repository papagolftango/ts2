# Engine control architecture notes

## Dual-core layout

- Core 0 handles real-time engine events, trigger-wheel capture, and ignition scheduling.
- Core 1 handles telemetry, logging, and diagnostic reporting.

This keeps the engine timing path deterministic while allowing slower tasks such as serial output, SD logging, or telemetry to run asynchronously.

## Trigger wheel

The initial experimental setup uses an optical trigger wheel. The starter code assumes a 36:1 optical pattern using 35 edges per revolution and one missing-slot or reference gap. This is a common layout for optical timing systems and should be confirmed against the actual wheel pattern before the final ignition logic is trusted.

The sensor should provide a clean digital signal suitable for interrupt capture on the ESP32. In practice, that usually means a logic-level pulse from a phototransistor, optocoupler, or comparator stage with a solid 3.3V-compatible output.

## Near-term roadmap

1. Verify trigger signal quality and timing jitter
2. Add missing-tooth detection and phase alignment
3. Compute RPM and crank angle accurately at each edge
4. Schedule spark output based on advance angle
5. Expand to full ignition mapping and knock correlation

## Interface pattern

The real-time task sends only compact state packets to the logger task over a queue. This pattern keeps the timing loop minimal and avoids long serial operations in the interrupt-driven path.
