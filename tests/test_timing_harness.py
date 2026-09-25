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


def main() -> None:
    print("Running timing harness checks...")
    verify_advance_curve()
    verify_spark_time_calc()
    simulate_missing_tooth_reference()

    for rpm in [800, 2000, 5000, 10000]:
        advance = lookup_advance_tenths_deg(rpm)
        period_us = (60_000_000 * 10) // (rpm * 35)
        spark_us = compute_spark_time_us(period_us, advance, MISSING_TOOTH_TO_TDC_OFFSET_DEG10)
        print(f"RPM={rpm:5d}  advance={advance/10:.1f}°  period={period_us}us  sparkWindow={spark_us}us")

    print("All timing harness checks passed.")


if __name__ == "__main__":
    main()
