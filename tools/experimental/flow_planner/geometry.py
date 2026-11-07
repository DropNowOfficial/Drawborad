"""Geometric contour helpers copied unchanged from reviewed v6, no palette code."""
import numpy as np
from scipy import ndimage as ndi

def contour_paths(mask):
    """4-connected cell-edge loops, expressed as inside pixel centers."""
    h, w = mask.shape
    edges = []
    outgoing = {}
    def at(x, y):
        return 0 <= x < w and 0 <= y < h and mask[y, x]
    def add(x, y, ix, iy, direction):
        outgoing[(x, y, direction)] = len(edges)
        edges.append((x, y, ix, iy, direction))
    for y, x in np.argwhere(mask):
        x, y = int(x), int(y)
        if not at(x, y - 1): add(x, y, x, y, 0)
        if not at(x + 1, y): add(x + 1, y, x, y, 1)
        if not at(x, y + 1): add(x + 1, y + 1, x, y, 2)
        if not at(x - 1, y): add(x, y + 1, x, y, 3)
    visited = set()
    paths = []
    dx, dy = (1, 0, -1, 0), (0, 1, 0, -1)
    for start in range(len(edges)):
        if start in visited: continue
        path = []
        current = start
        while current not in visited:
            visited.add(current)
            x, y, ix, iy, direction = edges[current]
            p = (ix, iy)
            if path and abs(path[-1][0] - ix) == 1 and abs(path[-1][1] - iy) == 1:
                # Do not cut the concave inside corner with a diagonal chord.
                a, b = (path[-1][0], iy), (ix, path[-1][1])
                if at(*a): path.append(a)
                elif at(*b): path.append(b)
            if not path or path[-1] != p: path.append(p)
            ex, ey = x + dx[direction], y + dy[direction]
            if (ex, ey) == edges[start][:2]:
                break
            candidates = [outgoing.get((ex, ey, (direction + turn) % 4)) for turn in (1, 0, 3, 2)]
            available = [i for i in candidates if i is not None and i not in visited]
            if not available:
                raise ValueError("open contour")
            current = available[0]
        if path[-1] != path[0]: path.append(path[0])
        if len(path) == 1: path.append(path[0])
        paths.append(path)
    return paths


def compact(path):
    result = []
    for point in path:
        if result and result[-1] == point: continue
        if len(result) > 1:
            a, b = result[-2], result[-1]
            dx, dy = b[0] - a[0], b[1] - a[1]
            ex, ey = point[0] - b[0], point[1] - b[1]
            if dx * ey == dy * ex and dx * ex + dy * ey > 0:
                result[-1] = point
                continue
        result.append(point)
    if len(result) == 1: result.append(result[0])
    return result


def pigment_paths(mask, color, max_width=40, boundary_padding=0):
    if not mask.any(): return []
    # Padding makes the document boundary a genuine boundary for the distance
    # field. No source pixels are persisted as brush coverage.
    distance = ndi.distance_transform_edt(np.pad(mask, 1))[1:-1, 1:-1]
    deepest = float(distance.max())
    level = 1.0
    paths = []
    while level <= deepest + 1e-6:
        width = min(max_width, max(1.0, 2 * level - 1 + boundary_padding))
        for path in contour_paths(distance >= level):
            points = [(x + .5, y + .5, 1.0) for x, y in compact(path)]
            paths.append(dict(color=color.tolist(), width=width, geometry=points, tool="marker"))
        level += max(1.0, min(level, max_width * .35))
    return paths

