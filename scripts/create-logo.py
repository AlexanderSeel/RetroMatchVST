"""Rebuild the original vector mark and its Windows/macOS icon raster."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

root = Path(__file__).resolve().parents[1] / "Assets"
root.mkdir(exist_ok=True)
# A continuous signal enters an R, then climbs the twin peaks of an M.
paths = [([(66, 286), (110, 286), (110, 152), (190, 152), (218, 180),
           (218, 218), (192, 244), (110, 244)], "#76ffe0"),
         ([(172, 244), (222, 310), (262, 310), (262, 164), (326, 242),
           (390, 164), (390, 310), (446, 310)], "#76ffe0")]
svg_paths = "\n".join(f'<polyline points="{" ".join(f"{x},{y}" for x,y in pts)}" fill="none" stroke="{color}" stroke-width="18" stroke-linecap="round" stroke-linejoin="round"/>' for pts,color in paths)
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
<defs><linearGradient id="metal" x2="0.2" y2="1"><stop stop-color="#64747b"/><stop offset=".46" stop-color="#202d34"/><stop offset="1" stop-color="#090f14"/></linearGradient></defs>
<rect x="12" y="12" width="488" height="488" rx="108" fill="#070b10"/>
<rect x="22" y="22" width="468" height="460" rx="96" fill="url(#metal)" stroke="#76888d" stroke-width="3"/>
<rect x="42" y="42" width="428" height="416" rx="77" fill="#09151b" stroke="#03090e" stroke-width="9"/>
<path d="M70 124H442 M70 350H442" stroke="#28423e" stroke-width="2"/>
{svg_paths}
<path d="M126 385H240" stroke="#f3c077" stroke-width="8" stroke-linecap="round"/>
<path d="M258 385H386" stroke="#466770" stroke-width="8" stroke-linecap="round"/>
<circle cx="256" cy="81" r="5" fill="#f3c077"/>
</svg>'''
(root / "retromatch-mark.svg").write_text(svg, encoding="utf-8")

scale = 2
im = Image.new("RGB", (512*scale, 512*scale), "#070b10")
d = ImageDraw.Draw(im)
box = lambda b: tuple(int(v*scale) for v in b)
d.rounded_rectangle(box((16,16,496,496)), radius=100*scale, fill="#33444e", outline="#8c9c9e", width=3*scale)
d.rounded_rectangle(box((34,34,478,478)), radius=85*scale, fill="#06131a", outline="#010609", width=8*scale)
glow = Image.new("RGBA", im.size)
gd = ImageDraw.Draw(glow)
for pts,color in paths:
    xy = [(x*scale,y*scale) for x,y in pts]
    gd.line(xy, fill=color, width=28*scale, joint="curve")
im = Image.alpha_composite(im.convert("RGBA"), glow.filter(ImageFilter.GaussianBlur(14*scale)))
d = ImageDraw.Draw(im)
for pts,color in paths:
    xy = [(x*scale,y*scale) for x,y in pts]
    d.line(xy, fill=color, width=18*scale, joint="curve")
    for x,y in (xy[0],xy[-1]): d.ellipse((x-9*scale,y-9*scale,x+9*scale,y+9*scale), fill=color)
d.line(box((126,385,240,385)), fill="#f3c077", width=8*scale)
d.line(box((258,385,386,385)), fill="#466770", width=8*scale)
d.ellipse(box((251,76,261,86)), fill="#f3c077")
im.save(root / "retromatch-icon.png")
