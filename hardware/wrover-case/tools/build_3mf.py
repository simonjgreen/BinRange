"""Pack ASCII STLs into one multi-object 3MF plate without third-party dependencies.

Usage: python3 build_3mf.py OUT.3mf "Name=part.stl" ["Name=part.stl" ...]

Each STL keeps its print orientation. Parts are laid out in a single column,
10 mm apart, centred on a 256 x 256 mm plate (Bambu Lab X1/P1/A1 size).
"""
import sys
import zipfile
from pathlib import Path
from xml.sax.saxutils import quoteattr

PLATE = 256
GAP = 10

CONTENT_TYPES = """<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
 <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
 <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>
</Types>
"""
RELS = """<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
 <Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>
</Relationships>
"""


def read_stl(path):
    points = [tuple(float(v) for v in line.split()[1:4])
              for line in Path(path).read_text().splitlines()
              if line.strip().startswith("vertex ")]
    assert points and len(points) % 3 == 0, f"{path}: not an ASCII triangle STL"
    index, vertices, triangles = {}, [], []
    for i in range(0, len(points), 3):
        tri = []
        for p in points[i:i+3]:
            if p not in index:
                index[p] = len(vertices)
                vertices.append(p)
            tri.append(index[p])
        triangles.append(tri)
    low = [min(v[k] for v in vertices) for k in range(3)]
    high = [max(v[k] for v in vertices) for k in range(3)]
    assert abs(low[2]) < 1e-6, f"{path}: part must sit at z=0"
    return vertices, triangles, low, high


def build(out, parts):
    meshes = [(name, *read_stl(stl)) for name, stl in parts]
    depth = sum(h[1]-l[1] for _, _, _, l, h in meshes) + GAP*(len(meshes)-1)
    width = max(h[0]-l[0] for _, _, _, l, h in meshes)
    assert depth <= PLATE-10 and width <= PLATE-10, "parts do not fit one plate"
    objects, items, y = [], [], (PLATE-depth)/2
    for oid, (name, vertices, triangles, low, high) in enumerate(meshes, 1):
        vx = "\n".join(f'     <vertex x="{x:.6g}" y="{v:.6g}" z="{z:.6g}"/>'
                       for x, v, z in vertices)
        tx = "\n".join(f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>'
                       for a, b, c in triangles)
        objects.append(f'  <object id="{oid}" name={quoteattr(name)} type="model">\n'
                       f'   <mesh>\n    <vertices>\n{vx}\n    </vertices>\n'
                       f'    <triangles>\n{tx}\n    </triangles>\n   </mesh>\n  </object>')
        dx = (PLATE-(high[0]-low[0]))/2 - low[0]
        dy = y - low[1]
        items.append(f'  <item objectid="{oid}" transform="1 0 0 0 1 0 0 0 1 {dx:.4f} {dy:.4f} 0"/>')
        y += high[1]-low[1] + GAP
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-GB" '
             'xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">\n'
             ' <metadata name="Title">WROVER case</metadata>\n'
             ' <resources>\n' + "\n".join(objects) + '\n </resources>\n'
             ' <build>\n' + "\n".join(items) + '\n </build>\n</model>\n')
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", CONTENT_TYPES)
        z.writestr("_rels/.rels", RELS)
        z.writestr("3D/3dmodel.model", model)
    print(f"{out}: {len(meshes)} objects, "
          + ", ".join(f"{n} {len(t)} triangles" for n, _, t, _, _ in meshes))


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    build(sys.argv[1], [arg.split("=", 1) for arg in sys.argv[2:]])
