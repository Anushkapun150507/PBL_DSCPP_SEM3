# How to edit UNIGO's data

All of UNIGO's data lives in this `data` folder as plain files. You can change them at any time.
**The running server picks up a saved change on the next search – no restart and no recompiling.**

| File | What it holds |
|---|---|
| `classrooms.csv` | Classrooms, labs, lecture theatres, offices (Find a Classroom / Lab) |
| `campus_map.json` | Buildings, roads, paths, the subway (Campus Map and Find a Route) |
| `stops.csv` | Bus timetable (Bus Finder) |
| `reviews.json` | Posted reviews (Reviews & Feedback); created by the first review |
| `lostfound/config.json` | Lost & Found settings: roll-number length, post lifetime, collection point, demo desk login |
| `lostfound/users.json`, `sessions.json` | UNIGO accounts and who is logged in (the whole site uses them); created by the website |
| `lostfound/posts.json`, `claims.json`, `photos/` | Lost & Found posts, claims and photos; created by the website |

---

## 1. Classrooms and labs – `classrooms.csv`

The current classroom data is **provisional** (partly user-provided, partly tentative). Correct it whenever you
learn the real information.

### Open and edit
- Open `classrooms.csv` in **Excel**, **Google Sheets** or **Notepad**.
- One row = one room or facility. Keep the first row (the column names) as it is.

| Column | Meaning | Example |
|---|---|---|
| `id` | Any unique code | `UNI047` |
| `building` | Building name – **must match a building on the map** (see below) | `CSIT Block` |
| `floor` | Floor, as you want it shown | `Second Floor` |
| `room_or_facility` | Room name – this is what students search | `CR 13` |
| `category` | Kind of room | `Classroom`, `Computer Laboratory` |
| `purpose` | One short sentence | `Lecture sessions` |
| `search_keywords` | Extra words students might type, separated by commas | `cr 13, classroom 13` |
| `data_status` | How sure we are, shown on the website exactly as written | `User-provided`, `Tentative - verify` |

### Common changes
- **Fix a floor or name:** edit the cell and save.
- **Add a room:** add a new row at the bottom.
- **Remove a room:** delete its row.
- **Several rooms on one row:** write them with commas, e.g. `CR 1, CR 2, CR 3` – the website shows them as separate rooms.
- **Mark a room as checked:** set `data_status` to `Verified` – only the exact word *Verified* shows the green
  "Verified" badge. Anything else (User-provided, Tentative – verify, Provisional …) is shown as written.

### Saving from Excel
Use **File → Save As → CSV UTF-8 (Comma delimited) (*.csv)** and keep the name `classrooms.csv`.

### Building names
The `building` value must be a building name or alias from `campus_map.json`, for example:
`CSIT Block`, `New CSIT Block`, `Main B.Tech Admin Block` (= Main Admin Block), `Civil Engineering Block`,
`Mechanical Workshop`, `Library Block` (= Santosh Anand Library), `Chanakya Block`, `MBA Block`, `School of Law`.
Capital letters, spaces, dots and hyphens don't matter.

### Check your changes
```
python tools/check_rooms.py
```
It lists rows without a building or room, building names that are not on the map, duplicates and missing floors.

---

## 2. The campus map – `campus_map.json`

- **Add another name for a building** (so classroom rows or searches can use it): add it to that place's `"aliases"` list.
- **Move or resize a building:** change its `"rect"`: `[x, y, width, height]` (the map is 1600 × 900).
- **Roads and paths:** each road is a list of junction ids in `"path"`; junction positions are in `"nodes"`.
  Remove a junction id from a road's path to remove that piece of road.

Always check after editing the map:
```
python tools/check_map.py
```
It reports roads passing through buildings, overlapping buildings, unused junctions and places that can't be reached.

---

## 3. Bus timetable – `stops.csv`

Edit `stops.csv` directly, or edit the Excel file and run `python tools/xlsx_to_csv.py`.

---

## 4. Reviews – `reviews.json`

This file is created by the website when the first review is posted. Each review is **one line**:
```
{"id":3,"name":"Quiet Panda 42","stars":4,"line":"Pretty good!","features":["map","bus"],"suggestion":"...","postedAt":"2026-11-09 14:50"},
```
- **Remove a review** (spam, rude words, a real name): delete its whole line. If it was the last line before `]`,
  also delete the comma at the end of the line above it. Save – it disappears from the website on the next visit.
- **Remove all reviews:** delete the file.
- Don't add reviews by hand: a line with a mistake is skipped, and the server says so in its window.

---

## 5. Lost & Found – the `lostfound` folder

**Settings – `lostfound/config.json`** (restart the server after changing it):
- `rollNumberDigits`: how many digits a roll number has (now 7).
- `postDays`: after how many days a post is archived as "Expired" (now 30).
- `collectionPoint`: where found items are handed in. `placeId` is a place id from `campus_map.json`, used for "Show on Campus Map".
- `demoAdmin`: the collection-desk login (`desk`). **Change the password before real use.** On the next start the server
  replaces the old desk password with the new one. Delete `demoAdmin` to have no desk account.

**Removing a post:** log in as the desk and press **Hide**. (Hidden posts disappear for students; the desk can show them again.)
Posts reported by 3 different students are hidden automatically until the desk checks them.

**Removing an account:** stop the server, delete the account's line in `lostfound/users.json`, start the server.
Passwords are not in that file, only salted hashes, so a forgotten password can't be read: delete the account and sign up again.

**Logging everyone out:** stop the server, delete `lostfound/sessions.json`, start the server.

**Starting fresh:** stop the server and delete `users.json`, `sessions.json`, `posts.json`, `claims.json` and the photos in `photos/`.
Keep `config.json`.

---

If a file has a mistake the server can't read, it keeps using the last good version and prints the reason
in the server window.
