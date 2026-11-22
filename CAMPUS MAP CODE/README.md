# UNIGO – Campus Map (Find a Route + Find a Classroom / Lab)

Part of **UNIGO**, a campus guide website for Graphic Era University (B.Tech PBL project).
This folder is the Campus Map feature on its own: it has its own frontend, backend and data, and runs by itself.

**Made by:** Sapna Yadav, Srishti Nahar

## What it does
An interactive SVG campus map with two tools: Find a Route (up to 4 walking routes between two buildings, shortest first) and Find a Classroom / Lab (search a room, see its building and floor, then get directions to it).

## How to run it
You need **g++** (MinGW on Windows) and a web browser.

**In VS Code:** open this folder, then *Terminal → Run Task… → UNIGO: Run server*, and open http://localhost:8080

**Or in a terminal, from this folder:**
```
g++ -std=c++14 -O2 -Wall -o backend/unigo_server.exe backend/server.cpp -lws2_32
backend\unigo_server.exe 8080 frontend data
```
Then open http://localhost:8080 . On Linux/macOS leave out `-lws2_32` and `.exe`.

Try it: Find a Route: from `Gate No. 2` to `Paramedical Block`. Find a Classroom / Lab: search `CR 1`, `LT 12` or `CS Lab 1`.

## Concepts used
- Graph stored as an adjacency list
- Dijkstra's shortest path with a priority queue (heap)
- Yen's algorithm for the 2nd, 3rd and 4th shortest routes
- JSON parser (recursive descent) for campus_map.json
- The shared search engine (hash tables + trie + edit distance) for places and rooms
- SVG drawing, zoom and click-to-pick in JavaScript

## Files
```
.gitignore
.vscode/tasks.json
README.md
backend/campus_handlers.h
backend/campus_map.h
backend/campus_store.h
backend/http.h
backend/json_reader.h
backend/json_writer.h
backend/room_database.h
backend/search_index.h
backend/server.cpp
backend/static_files.h
backend/text_utils.h
backend/trie.h
data/HOW_TO_EDIT.md
data/campus_map.json
data/classrooms.csv
frontend/css/style.css
frontend/index.html
frontend/js/app.js
frontend/js/campus-map.js
frontend/js/search-box.js
tools/check_map.py
tools/check_rooms.py
```

Frontend: HTML, CSS, JavaScript. Backend: C++14 with the standard library only (no external libraries).
Students' data created while using the site (accounts, posts, photos, reviews) is listed in `.gitignore` and must not be uploaded.
