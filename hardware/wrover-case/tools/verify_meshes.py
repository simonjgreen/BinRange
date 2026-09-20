"""Check the exported ASCII STLs without third-party dependencies."""
from collections import defaultdict
from pathlib import Path


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def check(path):
    vertices = [tuple(map(float, line.split()[1:]))
                for line in path.read_text().splitlines()
                if line.strip().startswith("vertex ")]
    assert vertices and len(vertices) % 3 == 0, f"{path}: not an ASCII triangle STL"
    triangles = [vertices[i:i+3] for i in range(0, len(vertices), 3)]
    edges = defaultdict(list)
    volume = 0
    for i, (a, b, c) in enumerate(triangles):
        normal = cross(sub(b, a), sub(c, a))
        assert sum(n*n for n in normal) > 1e-16, f"{path}: degenerate triangle {i}"
        volume += sum(x*y for x, y in zip(a, cross(b, c))) / 6
        for u, v in [(a, b), (b, c), (c, a)]:
            edges[tuple(sorted((u, v)))].append((i, u, v))
    adjacency = defaultdict(set)
    for edge, faces in edges.items():
        assert len(faces) == 2, f"{path}: non-manifold edge {edge}"
        (i, a, b), (j, c, d) = faces
        assert a == d and b == c, f"{path}: inconsistent winding at {edge}"
        adjacency[i].add(j)
        adjacency[j].add(i)
    visited, pending = set(), [0]
    while pending:
        current = pending.pop()
        if current not in visited:
            visited.add(current)
            pending.extend(adjacency[current] - visited)
    assert len(visited) == len(triangles), f"{path}: disconnected shells or sealed cavities"
    assert volume > 0, f"{path}: nonpositive signed volume"
    low = [min(v[k] for v in vertices) for k in range(3)]
    high = [max(v[k] for v in vertices) for k in range(3)]
    assert abs(low[2]) < 1e-6, f"{path}: print does not sit at z=0"
    size = [round(b-a, 3) for a, b in zip(low, high)]
    print(f"{path.name}: one closed, consistently oriented manifold shell; "
          f"{len(triangles)} triangles; bounds {size} mm; volume {volume:.1f} mm^3")


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        raise SystemExit("Usage: python3 verify_meshes.py <ASCII STL> [<ASCII STL> ...]")
    for arg in sys.argv[1:]:
        check(Path(arg))
