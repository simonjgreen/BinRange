"""Extract legacy Eagle mechanical data and make an annotated drawing.

Run with Python + matplotlib. Approximate component boxes are explicitly
separated from the source-derived outline and mounting-hole dimensions.
"""
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyBboxPatch, Rectangle

HERE = Path(__file__).parent
source = HERE / "makerfabs-esp32-uwb-v1.brd"
board = ET.parse(source).getroot().find("./drawing/board")
outline = [p for p in board.find("plain") if p.tag == "wire" and p.get("layer") == "20"]
points = [(float(p.get(f"x{i}")), float(p.get(f"y{i}"))) for p in outline for i in (1, 2)]
length = max(p[0] for p in points) - min(p[0] for p in points)
width = max(p[1] for p in points) - min(p[1] for p in points)
holes = [{k: float(p.get(k)) for k in ("x", "y", "radius")}
         for p in board.find("plain") if p.tag == "circle" and p.get("layer") == "20"]
data = {
    "source_url": "https://github.com/Makerfabs/Makerfabs-ESP32-UWB-DW3000/blob/main/hardware/ESP32%20UWB%20v1.0.brd",
    "retrieved": "2026-09-19",
    "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
    "units": "mm", "pcb_length": length, "pcb_width": width,
    "mounting_holes": holes,
    "hole_pitch": [max(h[k] for h in holes)-min(h[k] for h in holes) for k in ("x", "y")],
    "caution": "Legacy CAD; current USB-C revision only photo-cross-checked. Heights not specified by this outline."
}
(HERE / "board-dimensions.json").write_text(json.dumps(data, indent=2) + "\n")

fig, ax = plt.subplots(figsize=(12, 7.5), facecolor="#fafaf7")
ax.set_aspect("equal")
# Reverse both axes: component-side view rotated 180 degrees, matching the
# user's photo (USB at right, UWB antenna above), without mirroring the board.
ax.set_xlim(length + 15, -14)
ax.set_ylim(width + 20, -15)
ax.axis("off")
ink, blue, muted = "#223744", "#177889", "#5c666a"
ax.add_patch(FancyBboxPatch((0, 0), length, width, boxstyle="round,pad=0,rounding_size=1.3",
                          facecolor="#f3e5e1", edgecolor=ink, linewidth=1.8))
for h in holes:
    x, y, r = h["x"], h["y"], h["radius"]
    ax.add_patch(Circle((x, y), r, facecolor="#fafaf7", edgecolor=ink, lw=1.2))
    ax.plot([x-2.5,x+2.5],[y,y], color=muted, lw=.5)
    ax.plot([x,x],[y-2.5,y+2.5], color=muted, lw=.5)

def dim(a, b, label, vertical=False):
    ax.annotate("", xy=b, xytext=a, arrowprops=dict(arrowstyle="<->", lw=1, color=blue, shrinkA=0, shrinkB=0))
    x, y = (a[0]+b[0])/2, (a[1]+b[1])/2
    ax.text(x-1.4 if vertical else x, y if vertical else y-1.2, label,
            ha="right" if vertical else "center", va="center" if vertical else "bottom",
            rotation=90 if vertical else 0, color=blue, fontsize=11,
            bbox=dict(facecolor="#fafaf7", edgecolor="none", pad=1))

for x in (0, length): ax.plot([x,x],[-2,-11],color=blue,lw=.65)
dim((0,-9),(length,-9),f"{length:g} mm outline")
for y in (0,width): ax.plot([-10,-2],[y,y],color=blue,lw=.65)
dim((-8,0),(-8,width),f"{width:g} mm",True)
for x in (2,length-2): ax.plot([x,x],[width-1,width+10],color=blue,lw=.65)
dim((2,width+7),(length-2,width+7),f"{length-4:g} mm hole pitch")
for y in (2,width-2):ax.plot([length+1,length+9],[y,y],color=blue,lw=.65)
dim((length+7,2),(length+7,width-2),f"{width-4:g} mm pitch",True)

for (x,y,w,h,label) in [(40.3,7.04,25.2,18,"WROVER-E"),(65.5,7.04,6,18,"Wi-Fi"),
                       (19.36,9,13,14,"DW3000"),(19.36,.15,13,8.85,"UWB\nantenna"),
                       (-1,11.5,8.5,9,"USB-C")]:
    ax.add_patch(Rectangle((x,y),w,h,facecolor="#dae2e4",edgecolor=muted,lw=.7,linestyle="--"))
    ax.text(x+w/2,y+h/2,label,ha="center",va="center",fontsize=8,color=ink,
            rotation=90 if label=="Wi-Fi" else 0)
for x,y,label in [(2.4166,8.1851,"RST"),(2.5,24.532,"FLASH")]:
    ax.add_patch(Circle((x,y),1.25,facecolor=muted))
    ax.text(x+2,y,label,ha="right",va="center",fontsize=7,color=muted)
ax.text(40,-2,"UWB antenna edge (y = 0)",ha="center",fontsize=9,color=muted)
ax.text(36, width+13, "4 × Ø3 mm holes · centres 2 mm from each edge",ha="center",fontsize=11,color=ink)
fig.text(.07,.94,"WROVER ANCHOR / BOARD DIMENSIONS",fontsize=20,weight="bold",color=ink)
fig.text(.07,.895,"Makerfabs ESP32 UWB · source-derived outline and mounting pattern · all units mm",fontsize=11,color=muted)
fig.text(.07,.075,"Solid geometry: published v1.0 Eagle layer 20. Dashed boxes: approximate component locations.\n"
         "USB-C revision matches the supplied photo visually; confirm fit, heights and connector clearances on hardware.",
         fontsize=10,color=muted,linespacing=1.6)
fig.subplots_adjust(left=.04,right=.96,top=.86,bottom=.15)
fig.savefig(HERE / "board-dimensions.svg",facecolor=fig.get_facecolor())
fig.savefig(HERE / "board-dimensions.png",dpi=160,facecolor=fig.get_facecolor())
print(json.dumps(data, indent=2))
