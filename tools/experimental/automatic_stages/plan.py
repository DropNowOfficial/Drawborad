#!/usr/bin/env python3
"""Experimental automatic palette/form/structure planning, not a semantic artist.

Planning rasters remain temporary. The output contains only native brush paths,
colors, widths and pressure. The companion C++ verifier owns native rendering.
"""
import argparse
import json
import math
import sys
from pathlib import Path

import numpy as np
from scipy import ndimage as ndi

PHASES = ("base", "form", "detail")


def local_assignment(rgba, mask, centers):
    pixels = rgba[mask].astype(np.float64)
    scores = ((pixels[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
    for i, color in enumerate(centers):
        reliable = mask & (np.max(np.abs(rgba.astype(np.float64) - color), axis=2) < 32)
        distance = ndi.distance_transform_edt(~reliable) if reliable.any() else np.full(mask.shape, 32.0)
        # An ambiguous gray edge must not borrow a pigment from a remote
        # colored accent just because its global RGB distance is smaller.
        scores[:, i] += np.minimum(20, np.maximum(0, distance[mask] - 2)) ** 2 * 400
    return scores.argmin(axis=1)


def palette_labels(rgba, mask, max_colors=12):
    pixels = rgba[mask].astype(np.float64)
    bins = np.floor_divide(pixels.astype(np.int32), [8, 8, 8, 16])
    keys, inverse, count = np.unique(bins, axis=0, return_inverse=True, return_counts=True)
    means = np.array([np.bincount(inverse, weights=pixels[:, c]) / count for c in range(4)]).T
    # Frequent, separated source colors lead. Rare antialias mixtures do not
    # each acquire a paint color or a user-visible layer.
    centers = []
    for index in np.argsort(-count, kind="stable"):
        color = means[index]
        if count[index] < max(3, len(pixels) * .0004) and centers and \
                min(np.max(np.abs(color - c)) for c in centers) < 150:
            continue
        if not centers or min(np.linalg.norm((color - c) / [1, 1, 1, .8]) for c in centers) > 48:
            centers.append(color)
        if len(centers) == max_colors:
            break
    centers = np.asarray(centers)
    # Refine within source-derived cells, without inventing hue families.
    for _ in range(6):
        distances = ((means[:, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
        assigned = distances.argmin(axis=1)
        for i in range(len(centers)):
            selected = assigned == i
            if selected.any():
                centers[i] = np.average(means[selected], axis=0, weights=count[selected])
    labels = np.full(mask.shape, -1, np.int32)
    labels[mask] = local_assignment(rgba, mask, centers)
    counts = np.bincount(labels[mask], minlength=len(centers))
    dominant = int(counts.argmax())
    keep = []
    for i, color in enumerate(centers):
        field = ndi.distance_transform_edt(np.pad(labels == i, 1))[1:-1, 1:-1]
        core = int(np.sum(field > 1.8))
        # A one-pixel antialias mixture is not an independent paint region.
        # Preserve high-contrast marks and actual alpha differences, even when
        # tiny; discard only unsupported low-contrast palette families.
        if i == dominant or core >= 1 or color[3] < 250:
            keep.append(i)
    for i, color in enumerate(centers):
        if i not in keep and np.max(np.abs(color[:3] - centers[dominant, :3])) > 128 and \
                min(np.linalg.norm(color - centers[j]) for j in keep) > 80:
            keep.append(i)
    if len(keep) != len(centers):
        centers = centers[keep]
        labels[mask] = local_assignment(rgba, mask, centers)
    # Only merge tiny *low-contrast* fragments into touching regions. Isolated
    # high-contrast dots and fine lines are retained, not silently erased.
    for color in range(len(centers)):
        connected, total = ndi.label(labels == color)
        objects = ndi.find_objects(connected)
        for component in range(1, total + 1):
            box = objects[component - 1]
            area = connected[box] == component
            if area.sum() >= 5:
                continue
            selected = connected == component
            neighbors = labels[ndi.binary_dilation(selected) & mask & ~selected]
            choices = np.unique(neighbors[neighbors >= 0])
            if not len(choices):
                continue
            target = min(choices, key=lambda i: (np.linalg.norm(centers[i] - centers[color]), int(i)))
            if np.max(np.abs(centers[target] - centers[color])) <= 70:
                labels[selected] = target
    return centers / 255.0, labels


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


def skeleton(mask):
    """Deterministic Zhang-Suen thinning; no external tracing/model service."""
    image = mask.copy()
    for _ in range(max(mask.shape) * 2):
        changed = False
        for phase in (0, 1):
            p = np.pad(image, 1)
            n = [p[:-2, 1:-1], p[:-2, 2:], p[1:-1, 2:], p[2:, 2:],
                 p[2:, 1:-1], p[2:, :-2], p[1:-1, :-2], p[:-2, :-2]]
            count = sum(x.astype(np.uint8) for x in n)
            transitions = sum((~n[i] & n[(i + 1) % 8]).astype(np.uint8) for i in range(8))
            if phase == 0:
                corner = ~(n[0] & n[2] & n[4]) & ~(n[2] & n[4] & n[6])
            else:
                corner = ~(n[0] & n[2] & n[6]) & ~(n[0] & n[4] & n[6])
            remove = image & (count >= 2) & (count <= 6) & (transitions == 1) & corner
            if remove.any():
                image[remove] = False
                changed = True
        if not changed: return image
    raise ValueError("skeleton iteration budget")


def structure_paths(mask, color, max_width=40, reviewed_mask=None):
    if not mask.any(): return []
    distance = ndi.distance_transform_edt(np.pad(mask, 1))[1:-1, 1:-1]
    center = skeleton(mask)
    support = mask if reviewed_mask is None else reviewed_mask
    points = {(int(x), int(y)) for y, x in np.argwhere(center)}
    neighbors = {}
    for x, y in points:
        adjacent = []
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if not (dx or dy) or (x + dx, y + dy) not in points: continue
                if dx and dy and not (support[y, x + dx] or support[y + dy, x]): continue
                # A diagonal beside an existing orthogonal connection creates
                # triangular graph artifacts and needless tiny branch strokes.
                if dx and dy and ((x + dx, y) in points or (x, y + dy) in points): continue
                adjacent.append((x + dx, y + dy))
        neighbors[(x, y)] = sorted(adjacent, key=lambda p: (p[1], p[0]))
    used = set()
    paths = []
    starts = sorted(points, key=lambda p: (len(neighbors[p]) != 1, p[1], p[0]))
    for start in starts:
        if not neighbors[start]:
            paths.append([start, start])
        while any(tuple(sorted((start, p))) not in used for p in neighbors[start]):
            path = [start]
            current = start
            while True:
                options = [p for p in neighbors[current] if tuple(sorted((current, p))) not in used]
                if not options: break
                if len(path) > 1:
                    dx, dy = current[0] - path[-2][0], current[1] - path[-2][1]
                    options.sort(key=lambda p: (-(dx * (p[0] - current[0]) + dy * (p[1] - current[1])) /
                                               math.hypot(p[0] - current[0], p[1] - current[1]), p[1], p[0]))
                following = options[0]
                used.add(tuple(sorted((current, following))))
                path.append(following)
                current = following
            paths.append(path)
    strokes = []
    for path in paths:
        routed = [path[0]]
        for p in path[1:]:
            old = routed[-1]
            if old[0] != p[0] and old[1] != p[1] and not support[max(old[1], p[1]), max(old[0], p[0])]:
                choices = [q for q in ((old[0], p[1]), (p[0], old[1])) if support[q[1], q[0]]]
                if not choices: raise ValueError("disconnected skeleton diagonal")
                routed.append(max(choices, key=lambda q: distance[q[1], q[0]]))
            routed.append(p)
        path = routed
        radii = [min(max_width / 2, max(.5, distance[y, x] - .125)) for x, y in path]
        width = 2 * max(radii)
        geometry = [(x + .5, y + .5, r * 2 / width) for (x, y), r in zip(path, radii)]
        strokes.append(dict(color=color.tolist(), width=width, geometry=geometry, tool="pencil"))
    return strokes


def plan(rgba, masks, max_colors=12, max_events=2048, max_samples=250000):
    if rgba.ndim != 3 or rgba.shape[2] != 4 or rgba.dtype != np.uint8:
        raise ValueError("RGBA8 source required")
    h, w = rgba.shape[:2]
    if w < 1 or h < 1 or w * h > 512 * 512 or not 1 <= len(masks) <= 16:
        raise ValueError("prototype image/object budget")
    if not 2 <= max_colors <= 16: raise ValueError("palette budget")
    claimed = np.zeros((h, w), bool)
    stages = {phase: [] for phase in PHASES}
    objects = []
    for index, original in enumerate(masks):
        if original.shape != (h, w) or not np.isin(original, (0, 255)).all():
            raise ValueError("binary object mask required")
        mask = original != 0
        if not mask.any() or np.any(mask & claimed): raise ValueError("empty or overlapping masks")
        claimed |= mask
        colors, labels = palette_labels(rgba, mask, max_colors)
        counts = np.bincount(labels[mask], minlength=len(colors))
        opaque = rgba[:, :, 3] >= 254
        candidates = [i for i, c in enumerate(colors) if c[3] >= .98]
        base = max(candidates or list(range(len(colors))), key=lambda i: (counts[i], -i))
        luminance = colors[:, :3] @ [.2126, .7152, .0722]
        # These are appearance rules, not claims of anatomical/lighting understanding.
        dark = {i for i in range(len(colors)) if luminance[i] < luminance[base] - .28 and colors[i, 3] > .8}
        per_object = {phase: [] for phase in PHASES}
        approximated_pixels = 0
        omitted_micro_strokes = 0
        if np.any(mask & opaque):
            base_color = colors[base].copy();base_color[3] = 1.0
            per_object["base"] = pigment_paths(mask & opaque, base_color, boundary_padding=2 if np.all(opaque[mask]) else 0)
        for color in sorted(range(len(colors)), key=lambda i: (-counts[i], i)):
            region = labels == color
            if color == base and colors[color, 3] >= .98:
                region &= ~opaque
            if not region.any(): continue
            phase = "detail" if color in dark else "form"
            if phase == "detail":
                per_object[phase].extend(structure_paths(region, colors[color], reviewed_mask=mask))
            else:
                components, total = ndi.label(region, structure=np.ones((3, 3), np.uint8))
                for component in range(1, total + 1):
                    selected = components == component
                    area = int(selected.sum())
                    if area < 8 and np.max(np.abs(colors[color, :3] - colors[base, :3])) < .45 and colors[color, 3] >= .98:
                        # Deliberately tolerate low-contrast antialias specks
                        # over the base. They are not rendered as pixel dabs.
                        approximated_pixels += area
                        continue
                    field = ndi.distance_transform_edt(np.pad(selected, 1))[1:-1, 1:-1]
                    if field.max() <= 4:
                        planned = structure_paths(selected, colors[color], reviewed_mask=mask)
                    else:
                        planned = pigment_paths(selected, colors[color])
                    for stroke in planned:
                        length = sum(math.hypot(b[0]-a[0], b[1]-a[1]) for a, b in zip(stroke["geometry"], stroke["geometry"][1:]))
                        if len(planned) > 1 and length < 3 and stroke["width"] < 4 and \
                                np.max(np.abs(colors[color, :3] - colors[base, :3])) < .45 and colors[color, 3] >= .98:
                            # The base already exists. Do not chase low-value
                            # edge remnants with singleton paint events.
                            omitted_micro_strokes += 1
                            continue
                        per_object[phase].append(stroke)
        objects.append(dict(index=index, palette=colors.tolist(), palettePixels=counts.tolist(), basePalette=int(base),
                            approximatedLowContrastPixels=approximated_pixels, omittedMicroStrokes=omitted_micro_strokes))
        for phase in PHASES:
            for stroke in per_object[phase]:
                stroke.update(phase=phase, object=index)
                stages[phase].append(stroke)
    strokes = [stroke for phase in PHASES for stroke in stages[phase]]
    if not strokes or len(strokes) > max_events: raise ValueError("stroke event budget")
    if sum(len(s["geometry"]) for s in strokes) > max_samples: raise ValueError("geometry sample budget")
    # Validate every interpolated center, not only the stored endpoints. Native
    # object clipping protects footprints; temporary color regions stay private.
    for stroke in strokes:
        mask = masks[stroke["object"]]
        for a, b in zip(stroke["geometry"], stroke["geometry"][1:]):
            for t in np.linspace(0, 1, max(2, int(math.hypot(b[0]-a[0], b[1]-a[1])*2)+1)):
                x, y = a[0]+(b[0]-a[0])*t, a[1]+(b[1]-a[1])*t
                if not (0 <= x < w and 0 <= y < h and mask[int(y), int(x)]):
                    raise ValueError(f"stroke center left reviewed object mask: {stroke['phase']} object={stroke['object']} {a} -> {b} at {x},{y}")
    return dict(schema=1, width=w, height=h, phases=list(PHASES), objects=objects, strokes=strokes,
                provenance="automatic deterministic palette/geometry prototype; no hand-authored strokes or semantic model")


def write_plan(result, destination):
    destination.mkdir(parents=True, exist_ok=False)
    (destination / "plan.json").write_text(json.dumps(result, separators=(",", ":")), encoding="utf-8")
    with (destination / "plan.txt").open("w") as out:
        out.write(f"DBPAINT1 {result['width']} {result['height']} {len(result['objects'])} {len(result['strokes'])}\n")
        for s in result["strokes"]:
            out.write(f"{s['object']} {PHASES.index(s['phase'])} {s['width']:.12g} " +
                      " ".join(f"{v:.12g}" for v in s["color"]) + f" {len(s['geometry'])}\n")
            for x, y, pressure in s["geometry"]:
                out.write(f"{x:.12g} {y:.12g} {pressure:.12g}\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--experimental", action="store_true", help="Explicitly opt into this unintegrated research prototype")
    parser.add_argument("--diagnostic-unsupported", action="store_true", help="Generate labeled diagnostic output outside the eligible input contract")
    parser.add_argument("--rgba", type=Path, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--mask", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--colors", type=int, default=12)
    args = parser.parse_args()
    if not args.experimental:
        parser.error("This is an experimental prototype; --experimental is required. The stable planner is unchanged.")
    if args.width<1 or args.height<1 or args.width>512 or args.height>512 or not 1<=len(args.mask)<=16:
        parser.error("prototype dimension/object budget")
    def exact(path, count):
        if not path.is_file() or path.stat().st_size != count:raise ValueError("input must have the expected byte size")
        with path.open("rb") as handle:data=handle.read(count+1)
        if len(data)!=count:raise ValueError("input changed during read")
        return data
    rgba = np.frombuffer(exact(args.rgba,args.height*args.width*4), np.uint8).reshape(args.height, args.width, 4)
    masks = [np.frombuffer(exact(p,args.height*args.width), np.uint8).reshape(args.height, args.width) for p in args.mask]
    from contract import preflight
    report=preflight(rgba,masks)
    if not report["eligible"] and not args.diagnostic_unsupported:
        args.output.mkdir(parents=True,exist_ok=False)
        report["status"]="unsupported-input; no brush plan generated"
        (args.output/"preflight.json").write_text(json.dumps(report,indent=2))
        print(json.dumps(report),file=sys.stderr)
        raise SystemExit(3)
    result = plan(rgba, masks, args.colors)
    report["status"]="diagnostic-unsupported-input" if not report["eligible"] else "experimental-needs-review"
    result["preflight"]=report
    write_plan(result, args.output)
    (args.output/"preflight.json").write_text(json.dumps(report,indent=2))
    print(json.dumps({"events": len(result["strokes"]), "samples": sum(len(s["geometry"]) for s in result["strokes"]),
                      "phases": {phase: sum(s["phase"] == phase for s in result["strokes"]) for phase in PHASES}}))


if __name__ == "__main__": main()
