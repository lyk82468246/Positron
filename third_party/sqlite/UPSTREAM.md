# SQLite 3.53.4

This directory contains the official SQLite amalgamation used by
`positron_db.dll`.

- Source: [SQLite download page](https://sqlite.org/download.html)
- Archive: `sqlite-amalgamation-3530400.zip`
- SHA-256: `1E71DDF93849C6A6ECF58B827C0692073D2DD7EE40196158068F7B29F422E87D`.
- Build: `SQLITE_THREADSAFE=1`, `SQLITE_OMIT_WAL`,
  `SQLITE_MAX_MMAP_SIZE=0`, and `SQLITE_OMIT_LOAD_EXTENSION`.
- Target: Visual Studio 2008 / Windows Mobile 6 Professional SDK / ARMV4I.

SQLite is in the public domain. The amalgamation is kept as an upstream
source snapshot; Positron-specific behavior is implemented in
`positron_db/positron_db.c` and the project file, not by editing `sqlite3.c`.
