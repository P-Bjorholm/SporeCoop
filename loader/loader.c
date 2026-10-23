/*
 * SporeKit loader: proxy steam_api.dll.
 *
 * All Steam API exports are forwarded to steam_api_orig.dll by the .def file.
 * DllMain patches the exe entry point with a jump to our stub. The stub runs on
 * the main thread after every DLL is initialised (no loader lock), restores the
 * original bytes, loads sporekit.dll, calls sporekit_boot() and continues into
 * the original entry point.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef int(__cdecl *SporekitBootFn)(HMODULE loader);
typedef DWORD(WINAPI *EntryFn)(void *);

static HMODULE g_self;
static BYTE *g_entry;
static BYTE g_saved[5];

static void dbg(const char *s)
{
    OutputDebugStringA(s);
}

static void restore_entry(void)
{
    DWORD old;
    VirtualProtect(g_entry, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(g_entry, g_saved, 5);
    VirtualProtect(g_entry, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, 5);
}

/* An update could not replace a file the game had open (Data\SporeCoop.package): it left
 * <file>.new and listed the file (relative to the game folder) in mods\sporecoop.pending.
 * Here, before the game opens anything, the new files take their place. */
static void apply_pending(void)
{
    wchar_t root[MAX_PATH], list[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, root, MAX_PATH);
    int seps = 0;
    while (n > 0 && seps < 2) /* ...\SPORE\SporebinEP1\steam_api.dll -> ...\SPORE */
        if (root[--n] == L'\\' || root[n] == L'/')
            seps++;
    root[n] = 0;
    lstrcpyW(list, root);
    lstrcatW(list, L"\\SporebinEP1\\mods\\sporecoop.pending");
    HANDLE f = CreateFileW(list, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return;
    char buf[4096];
    DWORD got = 0;
    ReadFile(f, buf, sizeof buf - 1, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n' && *end != '\r')
            end++;
        char c = *end;
        *end = 0;
        /* plain relative names only (the updater writes them; never "..", drives or roots) */
        int bad = !*line || line[0] == '/' || line[0] == '\\';
        for (char *p = line; *p; p++)
            if (*p == ':' || (p[0] == '.' && p[1] == '.'))
                bad = 1;
        if (!bad) {
            wchar_t target[MAX_PATH * 2], fresh[MAX_PATH * 2], old[MAX_PATH * 2];
            lstrcpyW(target, root);
            lstrcatW(target, L"\\");
            int k = lstrlenW(target);
            for (char *p = line; *p && k < MAX_PATH * 2 - 8; p++)
                target[k++] = (wchar_t)(*p == '/' ? '\\' : (unsigned char)*p);
            target[k] = 0;
            lstrcpyW(fresh, target);
            lstrcatW(fresh, L".new");
            lstrcpyW(old, target);
            lstrcatW(old, L".old");
            if (GetFileAttributesW(fresh) != INVALID_FILE_ATTRIBUTES) {
                MoveFileExW(target, old, MOVEFILE_REPLACE_EXISTING);
                if (!MoveFileExW(fresh, target, MOVEFILE_REPLACE_EXISTING))
                    MoveFileExW(old, target, MOVEFILE_REPLACE_EXISTING); /* keep the game working */
                else
                    DeleteFileW(old);
            }
        }
        if (!c)
            break;
        line = end + 1;
    }
    DeleteFileW(list);
}

static void boot_sporekit(void)
{
    char env[16];
    if (GetEnvironmentVariableA("SPOREKIT_DISABLE", env, sizeof env) && env[0] == '1') {
        dbg("[sporekit-loader] disabled by SPOREKIT_DISABLE=1\n");
        return;
    }
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    while (n > 0 && path[n - 1] != L'\\' && path[n - 1] != L'/')
        n--;
    path[n] = 0;
    lstrcatW(path, L"sporekit.dll");
    HMODULE kit = LoadLibraryW(path);
    if (!kit) {
        dbg("[sporekit-loader] sporekit.dll not found, starting game without mods\n");
        return;
    }
    SporekitBootFn boot = (SporekitBootFn)(void *)GetProcAddress(kit, "sporekit_boot");
    if (!boot) {
        dbg("[sporekit-loader] sporekit_boot export missing\n");
        return;
    }
    boot(g_self);
}

static DWORD WINAPI entry_stub(void *peb)
{
    restore_entry();
    apply_pending();
    boot_sporekit();
    return ((EntryFn)(void *)g_entry)(peb);
}

static void patch_entry(void)
{
    BYTE *base = (BYTE *)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    DWORD old;
    if (!VirtualProtect(g_entry, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(g_saved, g_entry, 5);
    g_entry[0] = 0xE9;
    *(INT32 *)(g_entry + 1) = (INT32)((BYTE *)entry_stub - (g_entry + 5));
    VirtualProtect(g_entry, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), g_entry, 5);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        patch_entry();
    }
    return TRUE;
}
