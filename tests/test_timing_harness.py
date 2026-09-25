#!/usr/bin/env python3
"""Host-side safety harness for the ESP32 timing logic.

This script mirrors the integer-based timing math used in the firmware so the
logic can be validated without real-time embedded execution.
"""

from __future__ import annotations

MAX_RPM = 10000
CRANKING_RPM_THRESHOLD = 800
MIN_ADVANCE_TENTHS_DEG = 50
ADVANCE_TABLE_SIZE = 16
MISSING_TOOTH_GAP_FACTOR = 15
CDI_FIRE_DELAY_US = 250
MISSING_TOOTH_TO_TDC_OFFSET_DEG10 = 650

k_advance_table = [
    50, 60, 80, 100, 130, 160, 190, 220,
    250, 280, 300, 320, 335, 345, 350, 350,
]


def estimate_rpm_tenths(period_us: int) -> int:
    if period_us == 0:
        return 0
    return (60_000_000 * 10) // (period_us * 35)


def lookup_advance_tenths_deg(rpm: int) -> int:
    if rpm < CRANKING_RPM_THRESHOLD:
        return MIN_ADVANCE_TENTHS_DEG
    if rpm >= MAX_RPM:
        return k_advance_table[-1]

    table_index = (rpm * (ADVANCE_TABLE_SIZE - 1)) // MAX_RPM
    index = table_index
    next_index = index + 1 if index + 1 < ADVANCE_TABLE_SIZE else ADVANCE_TABLE_SIZE - 1

    low_advance = k_advance_table[index]
    high_advance = k_advance_table[next_index]

    rpm_low = (index * MAX_RPM) // (ADVANCE_TABLE_SIZE - 1)
    rpm_high = (next_index * MAX_RPM) // (ADVANCE_TABLE_SIZE - 1)

    if rpm_high == rpm_low:
        return low_advance

    fraction = ((rpm - rpm_low) << 16) // (rpm_high - rpm_low)
    interpolated = low_advance + (((high_advance - low_advance) * fraction) >> 16)
    return interpolated


def compute_spark_time_us(rev_period_us: int, advance_deg10: int, ref_offset_deg10: int) -> int:
    degrees_per_rev = 3600
    spark_angle_from_ref = advance_deg10 + ref_offset_deg10
    spark_fraction = (spark_angle_from_ref * rev_period_us) // degrees_per_rev
    return int(rev_period_us - spark_fraction - CDI_FIRE_DELAY_US)


def detect_missing_tooth(delta_us: int, last_good_period_us: int) -> bool:
    if last_good_period_us == 0:
        return False
    threshold = (last_good_period_us * MISSING_TOOTH_GAP_FACTOR) // 10
    return delta_us > threshold


def simulate_missing_tooth_reference() -> None:
    # Simulated wheel periods in us for a healthy toothed wheel followed by a missing tooth gap.
    wheel_periods = [
        9000, 9000, 9000, 9000, 9000,
        9000, 9000, 9000, 9000, 9000,
        9000, 9000, 9000, 9000,
    ]
    missing_gap = 15000
    deltas = wheel_periods + [missing_gap]

    last_good = 0
    detected = False

    for delta in deltas:
        if detect_missing_tooth(delta, last_good):
            detected = True
        last_good = delta

    assert detected, "missing tooth should be detected across a larger-than-normal gap"


def verify_advance_curve() -> None:
    previous = -1
    for rpm in [0, 300, 800, 1200, 2500, 5000, 8000, 10000]:
        value = lookup_advance_tenths_deg(rpm)
        assert value >= MIN_ADVANCE_TENTHS_DEG, f"advance should not go below {MIN_ADVANCE_TENTHS_DEG} tenths degrees"
        if previous >= 0:
            assert value >= previous, f"map must not go backwards with RPM: {previous} -> {value}"
        previous = value


def verify_spark_time_calc() -> None:
    assert 600 <= MISSING_TOOTH_TO_TDC_OFFSET_DEG10 <= 700, (
        "missing tooth offset should be in the 60-70° range; "
        f"got {MISSING_TOOTH_TO_TDC_OFFSET_DEG10 / 10:.1f}°"
    )
    rev_period_us = 10000
    advance_deg10 = lookup_advance_tenths_deg(5000)
    spark_us = compute_spark_time_us(rev_period_us, advance_deg10, MISSING_TOOTH_TO_TDC_OFFSET_DEG10)
    assert spark_us < rev_period_us, f"spark time should occur before the end of the rev: {spark_us} us"
    assert spark_us > 0, f"spark time should be positive: {spark_us} us"


def build_config_packet(
    version: int,
    missing_tooth_deg10: int,
    min_advance_deg10: int = 50,
    max_advance_deg10: int = 350,
    cdi_delay_us: int = 250,
    dwell_us: int = 2500,
    strobe_mask: int = 0,
    strobe_enabled: int = 0,
    strobe_pulse_ms: int = 5,
) -> bytes:
    packet = bytearray()
    packet.extend([0xA5, 0x10, 0x01, 0x0F])
    packet.extend((version & 0xFF, (version >> 8) & 0xFF))
    packet.extend((missing_tooth_deg10 & 0xFF, (missing_tooth_deg10 >> 8) & 0xFF))
    packet.extend((min_advance_deg10 & 0xFF, (min_advance_deg10 >> 8) & 0xFF))
    packet.extend((max_advance_deg10 & 0xFF, (max_advance_deg10 >> 8) & 0xFF))
    packet.extend((cdi_delay_us & 0xFF, (cdi_delay_us >> 8) & 0xFF))
    packet.extend((dwell_us & 0xFF, (dwell_us >> 8) & 0xFF))
    packet.append(strobe_mask)
    packet.append(strobe_enabled)
    packet.extend((strobe_pulse_ms & 0xFF, (strobe_pulse_ms >> 8) & 0xFF))
    packet.append(sum(packet[1:]) & 0xFF)
    return bytes(packet)


def parse_config_packet(raw: bytes) -> dict[str, int]:
    if len(raw) != 21:
        raise ValueError(f"expected 21-byte config packet, got {len(raw)}")
    if raw[0] != 0xA5 or raw[1] != 0x10:
        raise ValueError(f"invalid config frame: {raw!r}")
    checksum = sum(raw[1:-1]) & 0xFF
    if checksum != raw[-1]:
        raise ValueError(f"checksum mismatch: {raw!r}")
    version = raw[4] | (raw[5] << 8)
    offset = raw[6] | (raw[7] << 8)
    min_advance = raw[8] | (raw[9] << 8)
    max_advance = raw[10] | (raw[11] << 8)
    cdi_delay = raw[12] | (raw[13] << 8)
    dwell = raw[14] | (raw[15] << 8)
    strobe_mask = raw[16]
    strobe_enabled = raw[17]
    strobe_pulse_ms = raw[18] | (raw[19] << 8)
    return {
        "version": version,
        "missing_tooth": offset,
        "min_advance": min_advance,
        "max_advance": max_advance,
        "cdi_delay": cdi_delay,
        "dwell": dwell,
        "strobe_mask": strobe_mask,
        "strobe_enabled": strobe_enabled,
        "strobe_pulse_ms": strobe_pulse_ms,
    }


def build_telemetry_packet(rpm10: int, advance_deg10: int, crank_deg10: int) -> bytes:
    packet = bytearray()
    packet.extend([0xA5, 0x20, 0x01, 0x09])
    packet.extend((0x00, 0x00, 0x00, 0x00))
    packet.extend((rpm10 & 0xFF, (rpm10 >> 8) & 0xFF))
    packet.extend((advance_deg10 & 0xFF, (advance_deg10 >> 8) & 0xFF))
    packet.extend((crank_deg10 & 0xFF, (crank_deg10 >> 8) & 0xFF))
    packet.append(0x00)
    packet.append(sum(packet[1:]) & 0xFF)
    return bytes(packet)


def main() -> None:
    print("Running timing harness checks...")
    verify_advance_curve()
    verify_spark_time_calc()
    simulate_missing_tooth_reference()

    cfg = parse_config_packet(build_config_packet(7, 650, 50, 350, 250, 2500, 0x05, 1, 5))
    assert cfg["missing_tooth"] == 650, cfg
    assert cfg["version"] == 7, cfg
    assert cfg["min_advance"] == 50, cfg
    assert cfg["max_advance"] == 350, cfg
    assert cfg["cdi_delay"] == 250, cfg
    assert cfg["dwell"] == 2500, cfg
    assert cfg["strobe_mask"] == 0x05, cfg
    assert cfg["strobe_enabled"] == 1, cfg
    assert cfg["strobe_pulse_ms"] == 5, cfg

    telemetry = build_telemetry_packet(1800, 220, 90)
    assert telemetry[0] == 0xA5 and telemetry[1] == 0x20, telemetry
    assert telemetry[-1] == (sum(telemetry[1:-1]) & 0xFF), telemetry

    for rpm in [800, 2000, 5000, 10000]:
        advance = lookup_advance_tenths_deg(rpm)
        period_us = (60_000_000 * 10) // (rpm * 35)
        spark_us = compute_spark_time_us(period_us, advance, MISSING_TOOTH_TO_TDC_OFFSET_DEG10)
        print(f"RPM={rpm:5d}  advance={advance/10:.1f}°  period={period_us}us  sparkWindow={spark_us}us")

    print("All timing harness checks passed.")


if __name__ == "__main__":
    main()
