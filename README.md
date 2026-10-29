# Student_web
# Student Record Management Web App (C backend)

## Requirements
- MSYS2 UCRT64
- GCC
- SQLite development package for UCRT64

Install SQLite in the **MSYS2 UCRT64** terminal:
```bash
pacman -S --needed mingw-w64-ucrt-x86_64-sqlite3
```

## Build
From the UCRT64 terminal:
```bash
cd /d/DSA/student_web
gcc -Wall -Wextra server.c -o server.exe -lsqlite3 -lws2_32
```

## Run
```bash
./server.exe
```
Open http://localhost:8080 in your browser. Keep the terminal open while using the website.
Press Ctrl+C in the terminal to stop the server.

Student data is stored in `data/students.db`.
If port 8080 is already occupied, close the other local server and retry.

