# UNIGO – Lost & Found

Part of **UNIGO**, a campus guide website for Graphic Era University (B.Tech PBL project).
This folder holds only the Lost & Found feature's own code: its C++ files, its JavaScript, its part of the web page and its styles.

**Made by:** Vanshika

## What it does
Report something you lost or found, see possible matches, and claim an item ("This is mine"). The finder compares the owner's description with the item and approves the claim; only then is the item handed in at the CSIT collection desk, where the owner collects it with their college ID.

## How to run it
Lost & Found needs a logged-in student: every post and claim belongs to an account. The login is a separate part (**UNIGO_Reviews_Login**), so this folder does not run on its own. The whole website, with the login and every feature, runs from the full UNIGO project (`Open UNIGO.bat`, then http://localhost:8080).

In the full project these files sit in the same places: `backend/` and `frontend/js/` as here; the page part is inside `frontend/index.html` and the styles are inside `frontend/css/style.css`.

It also uses these shared files of the full project: `auth_handlers.h` and `auth_store.h` (login), `campus_store.h` and `campus_map.h` (places on the map), `search_index.h` (search), `crypto.h`, `file_utils.h`, `json_reader.h`, `json_writer.h`, `http.h`.

**Collection desk login:** copy `data/lostfound/config.example.json` to `data/lostfound/config.json`, replace `CHANGE-ME` with your own password and restart. Then log in with username `desk`.

## Server addresses (API)
- `GET /api/lostfound/meta` – categories, collection point, rules, counts
- `GET /api/lostfound/posts?type=found|lost` · `POST /api/lostfound/posts` (up to 2 JPEG photos)
- `GET /api/lostfound/mine` · `POST /api/lostfound/close` · `GET /api/lostfound/matches?post=<id>`
- `POST /api/lostfound/claims` · `POST /api/lostfound/report`
- `POST /api/lostfound/decide` – finder: approve / reject / atdesk; owner: returned
- Desk only: `GET /api/lostfound/admin` · `POST /api/lostfound/admin/decide` · `POST /api/lostfound/admin/hide`
- `GET /api/lostfound/photo?id=<32 hex>`

## Concepts used
- Classes LfPost and LfClaim; LostFoundStore keeps posts and claims in JSON files
- Matching lost and found posts: shared words, category, place and date window
- Claim states: pending -> approved -> at desk -> returned (or rejected)
- Only the finder can approve; only the owner can mark "I got it back" (checked on the server)
- Photos: shrunk in the browser, only real JPEG bytes accepted, random file names
- Privacy: other students never see a poster's roll number

## Files
```
.gitignore
README.md
backend/lostfound_handlers.h
backend/lostfound_store.h
data/lostfound/config.example.json
frontend/css/lost_found.css
frontend/js/lostfound.js
frontend/lost_found_page.html
```

Frontend: HTML, CSS, JavaScript. Backend: C++14 with the standard library only (no external libraries).
Students' posts, claims and photos are created while using the site and must not be uploaded.
