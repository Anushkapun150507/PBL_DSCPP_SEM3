"""Convert the 'Stop Data' sheet of the UNIGO Excel dataset to data/stops.csv.

Usage (from anywhere):  python tools/xlsx_to_csv.py [path/to/workbook.xlsx]
Needs: pip install openpyxl
The running server notices the new CSV automatically; no restart needed.
"""
import csv
import datetime
import sys
from pathlib import Path

import openpyxl

ROOT = Path(__file__).resolve().parent.parent
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "UNIGO_Bus_Dataset_Clean_Final.xlsx"
DST = ROOT / "data" / "stops.csv"


def cell_text(value):
    """Excel stores a typed time like 1:10 PM as a time object; write it back as '1:10 PM'."""
    if value is None:
        return ""
    if isinstance(value, datetime.datetime):
        value = value.time()
    if isinstance(value, datetime.time):
        hour = value.hour % 12 or 12
        return f"{hour}:{value.minute:02d} {'AM' if value.hour < 12 else 'PM'}"
    return str(value).strip()


sheet = openpyxl.load_workbook(SRC, data_only=True)["Stop Data"]
rows = [[cell_text(v) for v in row[:5]] for row in sheet.iter_rows(values_only=True)]
rows = [r for r in rows if any(r)]                      # drop fully blank rows

DST.parent.mkdir(exist_ok=True)
with open(DST, "w", newline="", encoding="utf-8") as out:
    csv.writer(out).writerows(rows)
print(f"Wrote {len(rows) - 1} places to {DST}")
