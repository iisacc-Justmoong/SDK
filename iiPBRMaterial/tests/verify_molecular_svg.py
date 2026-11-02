"""Parse an actual generated molecular SVG independently, without files or GUI."""
import math
import subprocess
import sys
import xml.etree.ElementTree as ET

raw = subprocess.check_output([sys.argv[1], '--emit-benzene-svg'], text=True)
root = ET.fromstring(raw)
ns = {'svg': 'http://www.w3.org/2000/svg'}
assert root.tag == '{http://www.w3.org/2000/svg}svg'
assert root.find('svg:title', ns).text == 'C6H6 molecular map'
viewbox = [float(x) for x in root.attrib['viewBox'].split()]
assert len(viewbox) == 4 and all(math.isfinite(x) for x in viewbox)
x, y, width, height = viewbox
assert width > 0 and height > 0
circles = root.findall('.//svg:circle', ns)
labels = root.findall('.//svg:text', ns)
lines = root.findall('.//svg:line', ns)
assert len(circles) == len(labels) == 6 and len(lines) == 12
assert sum('stroke-dasharray' in line.attrib for line in lines) == 6
assert len({group.attrib['data-atom'] for group in root.findall('svg:g', ns)}) == 6
for circle in circles:
    cx, cy, r = (float(circle.attrib[k]) for k in ('cx', 'cy', 'r'))
    assert r > 0 and x <= cx - r <= cx + r <= x + width
    assert y <= cy - r <= cy + r <= y + height
for line in lines:
    x1, y1, x2, y2 = (float(line.attrib[k]) for k in ('x1', 'y1', 'x2', 'y2'))
    assert all(math.isfinite(a) for a in (x1, y1, x2, y2))
    assert math.hypot(x1 - x2, y1 - y2) > 0

custom = ET.fromstring(subprocess.check_output([sys.argv[2], '--emit-custom-svg'], text=True))
assert custom.find('svg:title', ns).text == 'H2O{iisacc:example} molecular map'
custom_group = next(g for g in custom.findall('svg:g', ns) if g.get('data-custom-atom'))
assert custom_group.attrib['data-custom-atom'] == 'iisacc:example'
assert custom_group.find('svg:text', ns).text == 'X&H'

bond = ET.fromstring(subprocess.check_output([sys.argv[3], '--emit-custom-bond-svg'], text=True))
custom_lines = [line for line in bond.findall('.//svg:line', ns) if line.get('data-custom-bond') == 'test:binding']
assert len(custom_lines) == 3
assert sum(line.get('data-bond-arrow') == 'true' for line in custom_lines) == 2
assert sum('stroke-dasharray' in line.attrib for line in custom_lines) == 1
