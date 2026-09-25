from pathlib import Path
from math import sin, cos, radians

out = Path(r"c:\Users\paul.tindall\OneDrive - TTPGroup\Documents\ts2\docs\timing-wheel.svg")

cx, cy = 40, 40
r_outer = 39
r_hole = 6
r_arc = 31
r_tick_outer = 39
r_tick_inner = 34


def coord(angle_deg, radius):
    a = radians(angle_deg)
    x = cx - radius * sin(a)
    y = cy - radius * cos(a)
    return x, y


# Missing-tooth reference marker at the calibrated 65° offset from TDC.
missing_tick_outer = coord(65, r_tick_outer)
missing_tick_inner = coord(65, r_tick_inner - 2.5)

# Two longer color markers showing the operational ignition advance limits.
advance_min_tick_outer = coord(5, r_tick_outer)
advance_min_tick_inner = coord(5, r_tick_inner - 2.0)
advance_max_tick_outer = coord(35, r_tick_outer)
advance_max_tick_inner = coord(35, r_tick_inner - 2.0)

lines = []
for deg in range(0, 360, 10):
    a1 = coord(deg, r_tick_outer)
    a2 = coord(deg, r_tick_inner)
    lines.append(f'<line x1="{a1[0]:.3f}" y1="{a1[1]:.3f}" x2="{a2[0]:.3f}" y2="{a2[1]:.3f}" stroke="#2b3a47" stroke-width="0.45"/>')
    if deg % 20 == 0:
        txt = coord(deg, 31.8)
        lines.append(f'<text x="{txt[0]:.3f}" y="{txt[1]:.3f}" font-size="3.0" font-family="Arial, sans-serif" fill="#24384a" text-anchor="middle" dominant-baseline="middle">{deg}</text>')

for deg in range(5, 360, 10):
    a1 = coord(deg, r_tick_outer - 0.5)
    a2 = coord(deg, r_tick_inner + 1.2)
    lines.append(f'<line x1="{a1[0]:.3f}" y1="{a1[1]:.3f}" x2="{a2[0]:.3f}" y2="{a2[1]:.3f}" stroke="#4a5d6f" stroke-width="0.25"/>')

for deg in range(0, 41):
    if deg % 5 == 0:
        continue
    a1 = coord(deg, r_tick_outer - 0.3)
    a2 = coord(deg, r_tick_inner + 2.5)
    lines.append(f'<line x1="{a1[0]:.3f}" y1="{a1[1]:.3f}" x2="{a2[0]:.3f}" y2="{a2[1]:.3f}" stroke="#6a7d8f" stroke-width="0.18"/>')

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="80mm" height="80mm" viewBox="0 0 80 80">
  <rect width="80" height="80" fill="#f3f5f7"/>
  <circle cx="{cx}" cy="{cy}" r="{r_outer}" fill="none" stroke="#2b3a47" stroke-width="0.9"/>
  <line x1="{advance_min_tick_outer[0]:.3f}" y1="{advance_min_tick_outer[1]:.3f}" x2="{advance_min_tick_inner[0]:.3f}" y2="{advance_min_tick_inner[1]:.3f}" stroke="#7ec7a5" stroke-width="0.8"/>
  <line x1="{advance_max_tick_outer[0]:.3f}" y1="{advance_max_tick_outer[1]:.3f}" x2="{advance_max_tick_inner[0]:.3f}" y2="{advance_max_tick_inner[1]:.3f}" stroke="#7ec7a5" stroke-width="0.8"/>
  <line x1="{missing_tick_outer[0]:.3f}" y1="{missing_tick_outer[1]:.3f}" x2="{missing_tick_inner[0]:.3f}" y2="{missing_tick_inner[1]:.3f}" stroke="#f29d4b" stroke-width="0.9"/>
  <circle cx="{cx}" cy="{cy}" r="{r_hole}" fill="#f3f5f7" stroke="#2b3a47" stroke-width="0.7"/>
  {' '.join(lines)}
</svg>
'''
out.write_text(svg, encoding='utf-8')
print(f"Wrote {out}")
