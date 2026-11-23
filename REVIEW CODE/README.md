# UNIGO – Reviews & Feedback + Sign up / Log in

Part of **UNIGO**, a campus guide website for Graphic Era University (B.Tech PBL project).
This folder is the Reviews & Feedback + Sign up / Log in feature on its own: it has its own frontend, backend and data, and runs by itself.

**Made by:** Sapna Yadav, Srishti Nahar (Reviews & Feedback); Vanshika, Anushka (Sign up / Log in)

## What it does
Sign up with name, university roll number and password, or log in with roll number and password. Then rate UNIGO with 1-5 stars (each rating gets a review line), choose the features you find most useful, optionally suggest improvements, and post publicly under an anonymous username. The wall shows the average rating, the count per star and every review.

## How to run it
You need **g++** (MinGW on Windows) and a web browser.

**In VS Code:** open this folder, then *Terminal → Run Task… → UNIGO: Run server*, and open http://localhost:8080

**Or in a terminal, from this folder:**
```
g++ -std=c++14 -O2 -Wall -o backend/unigo_server.exe backend/server.cpp -lws2_32
backend\unigo_server.exe 8080 frontend data
```
Then open http://localhost:8080 . On Linux/macOS leave out `-lws2_32` and `.exe`.

Try it: Sign up with a 7-digit roll number, then give 4 stars, post as `Quiet Panda 42`, and see it on the wall.

## Concepts used
- Password hashing: PBKDF2-HMAC-SHA256 with a random salt (crypto.h, written from the standards)
- Sessions: random 256-bit token in an HttpOnly cookie; only its SHA-256 fingerprint is saved
- Lock for 15 minutes after 5 wrong passwords (deque of failure times)
- Roll number, name and password rules checked on the page and again on the server
- Reviews: average rating, count per star and per feature, duplicate check
- JSON reading and writing, safe (atomic) file saving

## Files
```
.gitignore
.vscode/tasks.json
README.md
backend/auth_handlers.h
backend/auth_store.h
backend/crypto.h
backend/file_utils.h
backend/http.h
backend/json_reader.h
backend/json_writer.h
backend/review_handlers.h
backend/review_store.h
backend/server.cpp
backend/static_files.h
backend/text_utils.h
frontend/css/style.css
frontend/index.html
frontend/js/app.js
frontend/js/reviews.js
```

Frontend: HTML, CSS, JavaScript. Backend: C++14 with the standard library only (no external libraries).
Students' data created while using the site (accounts, posts, photos, reviews) is listed in `.gitignore` and must not be uploaded.
