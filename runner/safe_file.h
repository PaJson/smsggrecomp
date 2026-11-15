/* safe_file.h - replace a file without risking the old one.
 *
 * safe_fopen_w(path, mode) opens "<path>.tmp" for writing; safe_fclose(f, path) flushes it to the
 * disk, closes it and only then replaces <path> with it in one step (MoveFileEx with
 * MOVEFILE_REPLACE_EXISTING on Windows, rename() elsewhere). A crash, a full disk or a power cut
 * while writing leaves the previous <path> intact (at worst a stale "<path>.tmp" stays behind,
 * which the next save overwrites). Used for saves (.sav, .automap, .gvmap, .password) and the
 * settings files. */
#pragma once
#include <stdio.h>
#include <stdbool.h>

FILE *safe_fopen_w(const char *path, const char *mode);   /* "w" or "wb"; NULL on failure */
bool  safe_fclose(FILE *f, const char *path);             /* false = nothing replaced (old file kept) */
