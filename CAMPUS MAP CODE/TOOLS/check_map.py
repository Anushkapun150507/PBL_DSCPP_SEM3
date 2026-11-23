"""Check data/campus_map.json after editing it.

Usage:  python tools/check_map.py
Checks that:
  * every road and place refers to junctions that exist, ids are unique and no junction is left unused,
  * no road, path or subway passes through (or too close to) a building,
  * no two buildings overlap,
  * a building's door connects to its road without crossing another building,
  * every place can be reached from Gate No. 1 (the road network is connected).
Exits with code 1 and lists the problems if anything is wrong.
"""
import json
import math
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAP = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "data" / "campus_map.json"
CLEARANCE = {"road": 9, "path": 5, "subway": 5}   # minimum gap between a line's centre and a building


def seg_point_dist(p, a, b):
    ax, ay = a; bx, by = b; px, py = p
    dx, dy = bx - ax, by - ay
    if dx == dy == 0:
        return math.hypot(px - ax, py - ay)
    t = max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def segs_intersect(p1, p2, p3, p4):
    def orient(a, b, c):
        return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    d1, d2 = orient(p3, p4, p1), orient(p3, p4, p2)
    d3, d4 = orient(p1, p2, p3), orient(p1, p2, p4)
    return (d1 > 0) != (d2 > 0) and (d3 > 0) != (d4 > 0)


def seg_rect_dist(a, b, r):
    x, y, w, h = r
    corners = [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    inside = lambda p: x <= p[0] <= x + w and y <= p[1] <= y + h
    if inside(a) or inside(b):
        return 0.0
    edges = list(zip(corners, corners[1:] + corners[:1]))
    if any(segs_intersect(a, b, c, d) for c, d in edges):
        return 0.0
    return min([seg_point_dist(c, a, b) for c in corners] + [seg_point_dist(a, c, d) for c, d in edges] +
               [seg_point_dist(b, c, d) for c, d in edges])


def rects_overlap(r1, r2):
    return not (r1[0] + r1[2] <= r2[0] or r2[0] + r2[2] <= r1[0] or r1[1] + r1[3] <= r2[1] or r2[1] + r2[3] <= r1[1])


def main():
    data = json.loads(MAP.read_text(encoding="utf-8"))
    nodes, roads, places = data["nodes"], data["roads"], data["places"]
    problems = []

    ids = [p["id"] for p in places]
    for dup in {i for i in ids if ids.count(i) > 1}:
        problems.append(f"duplicate place id: {dup}")
    for r in roads:
        for n in r["path"]:
            if n not in nodes:
                problems.append(f"road '{r['name']}' uses unknown junction {n}")
    for p in places:
        if p["link"] not in nodes:
            problems.append(f"place '{p['name']}' links to unknown junction {p['link']}")
    used = {n for r in roads for n in r["path"]} | {p["link"] for p in places}
    for n in nodes:
        if n not in used:
            problems.append(f"junction {n} is not on any road (remove it, or a tap on the map could snap to it)")
    if problems:
        return report(problems)

    segments = [(r["kind"], r["name"], nodes[a], nodes[b]) for r in roads for a, b in zip(r["path"], r["path"][1:])]
    buildings = [p for p in places if "rect" in p]

    for p in buildings:
        for kind, name, a, b in segments:
            d = seg_rect_dist(a, b, p["rect"])
            if d < CLEARANCE[kind]:
                problems.append(f"{kind} '{name}' {a}->{b} is {d:.1f}px from '{p['name']}' (needs {CLEARANCE[kind]})")
        door, link = p["door"], nodes[p["link"]]
        for q in buildings:
            if q is not p and seg_rect_dist(door, link, q["rect"]) == 0:
                problems.append(f"door path of '{p['name']}' crosses '{q['name']}'")
    for i, p in enumerate(buildings):
        for q in buildings[i + 1:]:
            if rects_overlap(p["rect"], q["rect"]):
                problems.append(f"'{p['name']}' overlaps '{q['name']}'")

    adj = {n: set() for n in nodes}
    for r in roads:
        for a, b in zip(r["path"], r["path"][1:]):
            adj[a].add(b); adj[b].add(a)
    start = next(p["link"] for p in places if p["id"] == "gate-1")
    seen, queue = {start}, deque([start])
    while queue:                                   # breadth-first search over the road graph
        for nb in adj[queue.popleft()]:
            if nb not in seen:
                seen.add(nb); queue.append(nb)
    for p in places:
        if p["link"] not in seen:
            problems.append(f"'{p['name']}' cannot be reached from Gate No. 1")

    print(f"{len(nodes)} junctions, {len(segments)} road segments, {len(places)} places checked.")
    return report(problems)


def report(problems):
    if problems:
        print(f"{len(problems)} problem(s):")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    print("Map OK: no road crosses a building, nothing overlaps, every place is reachable.")


if __name__ == "__main__":
    main()
