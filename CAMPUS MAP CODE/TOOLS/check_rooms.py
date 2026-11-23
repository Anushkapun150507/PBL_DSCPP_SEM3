"""Check data/classrooms.csv after editing it.

Usage:  python tools/check_rooms.py
Reports:
  * missing columns, and rows with no building or no room name (these rows are skipped by the website),
  * building names that don't match any building on the campus map (add the name as an alias in
    data/campus_map.json, or fix the spelling in the CSV),
  * the same room listed twice in the same building,
  * how many rows have each data_status.
Exits with code 1 if something must be fixed.
"""
import csv
import json
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ROOMS = ROOT / "data" / "classrooms.csv"
MAP = ROOT / "data" / "campus_map.json"
REQUIRED = ["building", "room_or_facility"]
OPTIONAL = ["id", "floor", "category", "purpose", "search_keywords", "data_status"]


def compact(text):
    """Same rule as the C++ code: lowercase, no spaces, dots, hyphens, slashes or underscores."""
    return re.sub(r"[\s.\-/_]", "", text.strip().lower())


def main():
    data = json.loads(MAP.read_text(encoding="utf-8"))
    known = {}
    for p in data["places"]:
        for name in [p["name"]] + p.get("aliases", []):
            known[compact(name)] = p["name"]

    with open(ROOMS, encoding="utf-8-sig", newline="") as f:
        rows = list(csv.DictReader(f))
    header = [h.strip().lower() for h in (rows[0].keys() if rows else [])]
    problems, warnings = [], []
    for col in REQUIRED:
        if col not in header:
            problems.append(f"missing column '{col}' (columns found: {', '.join(header)})")
    if problems:
        return report(problems, warnings, rows)

    seen = Counter()
    for n, row in enumerate(rows, start=2):                  # line 1 is the header
        row = {k.strip().lower(): (v or "").strip() for k, v in row.items() if k}
        building, room = row.get("building", ""), row.get("room_or_facility", "")
        if not building or not room:
            problems.append(f"line {n}: needs both a building and a room name (this row is skipped by the website)")
            continue
        if compact(building) not in known:
            problems.append(f"line {n}: building '{building}' is not on the campus map "
                            "(fix the spelling, or add it as an alias of a place in data/campus_map.json)")
        seen[(compact(building), compact(room))] += 1
        if not row.get("floor"):
            warnings.append(f"line {n}: '{room}' has no floor")
        if not row.get("data_status"):
            warnings.append(f"line {n}: '{room}' has no data_status (it will show 'Status not given')")
    for (b, r), count in seen.items():
        if count > 1:
            warnings.append(f"'{r}' appears {count} times in the same building")
    return report(problems, warnings, rows)


def report(problems, warnings, rows):
    statuses = Counter((r.get("data_status") or "").strip() or "(empty)" for r in rows)
    print(f"{len(rows)} rows checked.")
    print("data_status counts: " + ", ".join(f"{s}: {c}" for s, c in statuses.most_common()))
    for w in warnings:
        print("  note:", w)
    if problems:
        print(f"{len(problems)} problem(s) to fix:")
        for p in problems:
            print("  -", p)
        sys.exit(1)
    print("Classroom data OK: every row has a building and a room, and every building is on the map.")


if __name__ == "__main__":
    main()
