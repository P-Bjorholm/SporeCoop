/*
 * The deferred files of an update (BUG-0150), shared by the loader (loader.c, plain C) and the
 * updater (winupdate.cpp apply_pending): an update could not replace a file the game held open
 * (Data\SporeCoop.package), so it left <file>.new and listed the file, relative to the game folder,
 * one per line in SporebinEP1\mods\sporecoop.pending. Before the next start the new files take
 * their place. An entry whose swap fails again (another instance on the same game folder still
 * holds the file, a scanner locks it) stays in the list for the start after that; the list is
 * deleted only when nothing is left. Before, the list was deleted whatever happened and the .new
 * file was never applied. The replaced file is kept as <file>.sporecoop-old (removed by
 * upd::cleanup_old; the loader removes it at once when it can).
 *
 * Only Win32 calls, no C runtime: the loader is a small proxy DLL.
 */
#ifndef SPORECOOP_PENDING_H
#define SPORECOOP_PENDING_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef SC_OLD_SUFFIX
#define SC_OLD_SUFFIX L".sporecoop-old"
#endif

/* tests (BUG-0150): 0 = the old behaviour (the list is deleted whatever the swaps did) */
static int sc_pending_keep_failed = 1;

/* applies the list in <root>\SporebinEP1\mods\sporecoop.pending; returns how many entries wait
 * for a later start (-1: no list) */
static int sc_apply_pending(const wchar_t *root, int delete_old)
{
    wchar_t list[MAX_PATH * 2];
    HANDLE f;
    DWORD size, got = 0, kept_len = 0, written;
    char *buf, *kept, *line;
    int left = 0;
    if (lstrlenW(root) > MAX_PATH)
        return -1;
    lstrcpyW(list, root);
    lstrcatW(list, L"\\SporebinEP1\\mods\\sporecoop.pending");
    f = CreateFileW(list, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > (1u << 20)) { /* the whole list (was: 4096 bytes) */
        CloseHandle(f);
        return -1;
    }
    buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    kept = (char *)HeapAlloc(GetProcessHeap(), 0, size + 2);
    if (!buf || !kept) {
        CloseHandle(f);
        if (buf)
            HeapFree(GetProcessHeap(), 0, buf);
        if (kept)
            HeapFree(GetProcessHeap(), 0, kept);
        return -1;
    }
    if (!ReadFile(f, buf, size, &got, NULL))
        got = 0;
    CloseHandle(f);
    buf[got] = 0;
    line = buf;
    while (*line) {
        char *end = line, *p, c;
        int bad, k, waits = 0;
        wchar_t target[MAX_PATH * 2], fresh[MAX_PATH * 2], old[MAX_PATH * 2];
        while (*end && *end != '\n' && *end != '\r')
            end++;
        c = *end;
        *end = 0;
        /* plain relative names only (the updater writes them; never "..", drives or roots) */
        bad = !*line || line[0] == '/' || line[0] == '\\' || end - line > MAX_PATH - 20;
        for (p = line; *p; p++)
            if (*p == ':' || (p[0] == '.' && p[1] == '.'))
                bad = 1;
        if (!bad) {
            lstrcpyW(target, root);
            lstrcatW(target, L"\\");
            k = lstrlenW(target);
            for (p = line; *p && k < MAX_PATH * 2 - 20; p++)
                target[k++] = (wchar_t)(*p == '/' ? '\\' : (unsigned char)*p);
            target[k] = 0;
            lstrcpyW(fresh, target);
            lstrcatW(fresh, L".new");
            lstrcpyW(old, target);
            lstrcatW(old, SC_OLD_SUFFIX);
            if (GetFileAttributesW(fresh) != INVALID_FILE_ATTRIBUTES) {
                int had = GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES;
                if (had && !MoveFileExW(target, old, MOVEFILE_REPLACE_EXISTING)) {
                    waits = 1; /* still open somewhere: the next start */
                } else if (!MoveFileExW(fresh, target, MOVEFILE_REPLACE_EXISTING)) {
                    if (had)
                        MoveFileExW(old, target, MOVEFILE_REPLACE_EXISTING); /* keep the game working */
                    waits = 1;
                } else if (delete_old && had) {
                    DeleteFileW(old);
                }
            }
            if (waits) {
                for (p = line; *p; p++)
                    kept[kept_len++] = *p;
                kept[kept_len++] = '\n';
                left++;
            }
        }
        if (!c)
            break;
        line = end + 1;
    }
    if (left && sc_pending_keep_failed) {
        f = CreateFileW(list, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, kept, kept_len, &written, NULL);
            CloseHandle(f);
        }
    } else {
        DeleteFileW(list);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    HeapFree(GetProcessHeap(), 0, kept);
    return sc_pending_keep_failed ? left : 0;
}

#endif
