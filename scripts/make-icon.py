"""Render the rectangles, circle and diagonal gradient in app.svg with Pillow."""
from pathlib import Path
import xml.etree.ElementTree as ET
from PIL import Image, ImageColor, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
SCALE = 4
svg = ET.parse(ROOT / "resources/app.svg").getroot()
size = int(svg.attrib["width"])
base = Image.new("RGBA", (size * SCALE, size * SCALE))
stops = svg.findall(".//{*}stop")
colors = [ImageColor.getrgb(stop.attrib["stop-color"]) for stop in stops]

for shape in svg:
    kind = shape.tag.rsplit("}", 1)[-1]
    if kind == "defs":
        continue
    values = {key: float(value) for key, value in shape.attrib.items() if key != "fill"}
    mask = Image.new("L", base.size)
    draw = ImageDraw.Draw(mask)
    if kind == "rect":
        x, y, width, height = (values[key] for key in ("x", "y", "width", "height"))
        draw.rounded_rectangle(tuple(v * SCALE for v in (x, y, x + width, y + height)),
                               radius=values.get("rx", 0) * SCALE, fill=255)
    elif kind == "circle":
        x, y, radius = (values[key] for key in ("cx", "cy", "r"))
        draw.ellipse(tuple(v * SCALE for v in (x - radius, y - radius, x + radius, y + radius)), fill=255)
    else:
        raise ValueError(f"Unsupported icon shape: {kind}")
    fill = shape.attrib["fill"]
    if fill == "url(#bg)":
        gradient = Image.new("RGB", (size, size))
        gradient.putdata([tuple(round(a + (b - a) * min(1, max(0, ((px - x) / width + (py - y) / height) / 2)))
                                for a, b in zip(*colors)) for py in range(size) for px in range(size)])
        fill = gradient.resize(base.size, Image.Resampling.BICUBIC)
    base.paste(fill, (0, 0, *base.size), mask)

base = base.resize((size, size), Image.Resampling.LANCZOS)
base.save(ROOT / "resources/app.ico", sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)])
print("Generated app.ico at 16-256 px from resources/app.svg.")
