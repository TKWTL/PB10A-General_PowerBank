#!/usr/bin/env python3
"""Generate the integer WLED lookup tables from the measured 30 C model."""

POINTS = ((0, 0, 0), (64, 1346, 280),
          (256, 3208, 450), (576, 7023, 950))


def interpolate(value, x, y, round_up=False):
    for a, b in zip(POINTS, POINTS[1:]):
        if value <= b[x]:
            span = b[x] - a[x]
            product = (value - a[x]) * (b[y] - a[y])
            return a[y] + (product + (span - 1 if round_up else 0)) // span
    return POINTS[-1][y]


def ambient_budget(temp):
    rise = max(0, 700 - temp)
    for a, b in zip(POINTS, POINTS[1:]):
        if rise <= b[2]:
            return a[1] + (rise - a[2]) * (b[1] - a[1]) // (b[2] - a[2])
    return POINTS[-1][1]


def hotspot_limit(temp):
    for threshold, level in ((550, 19), (600, 17), (650, 16),
                             (700, 15), (730, 12), (779, 8)):
        if temp <= threshold:
            return level
    return 0


power = [interpolate(level * level, 0, 1) for level in range(20)]
rise = [interpolate(mw, 1, 2, round_up=True) for mw in power]


def manual_limit(temp):
    return max(level for level in range(20)
               if power[level] <= ambient_budget(temp)
               and level <= hotspot_limit(temp + rise[level]))


temperatures = range(300, 701)
thresholds = [max(temp for temp in temperatures if manual_limit(temp) >= level)
              for level in range(1, manual_limit(300) + 1)]
for temp in temperatures:
    assert sum(temp <= threshold for threshold in thresholds) == manual_limit(temp)
print("s_manual_power_mw:", power)
print("s_level_temp_max:", thresholds)
print("s_level_rise_c10:", rise)
