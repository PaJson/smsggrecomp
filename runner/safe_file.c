/* safe_file.c - see safe_file.h */
#include "safe_file.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

static bool tmp_name(char *out, size_t n, const char *path)
{
    int k = snprintf(out, n, "%s.tmp", path);
    return path && path[0] && k > 0 && (size_t)k < n;
}

FILE *safe_fopen_w(const char *path, const char *mode)
{
    char tmp[1200];
    if (!tmp_name(tmp, sizeof tmp, path)) return NULL;
    return fopen(tmp, mode);
}

bool safe_fclose(FILE *f, const char *path)
{
    char tmp[1200];
    if (!f) return false;
    bool ok = tmp_name(tmp, sizeof tmp, path);
    ok = fflush(f) == 0 && ok;
    ok = !ferror(f) && ok;
#ifdef _WIN32
    if (ok) ok = _commit(_fileno(f)) == 0;          /* the data is on the disk before the swap */
#else
    if (ok) ok = fsync(fileno(f)) == 0;
#endif
    ok = fclose(f) == 0 && ok;
    if (!ok) { remove(tmp); return false; }
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) { remove(tmp); return false; }
#else
    if (rename(tmp, path) != 0) { remove(tmp); return false; }
#endif
    return true;
}
