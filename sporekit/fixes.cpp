// Fix switches for tests (kit.fix), per-thread flags of the fault logging, and the test commands
// that force the conditions of the freezes fixed in wave 1 (BUG-0050, 0065, 0115, 0132).
#include "core.h"

#include <cstdio>
#include <cstring>

namespace sk {

KitFix g_fix_stackdump{"stackdump", "stack / hangdump / watchdog: copy the stack, resume, then format (BUG-0050)", 1};
KitFix g_fix_cheatwait{"cheatwait", "commands that wait for the main thread refuse to run on it (BUG-0065)", 1};
KitFix g_fix_reentry{"reentry", "a fault inside the fault logger or the log is not logged again (BUG-0115)", 1};
KitFix g_fix_hookkeep{"hookkeep", "removed hooks keep their trampolines, a grace before FreeLibrary (BUG-0132)", 1};

std::vector<KitFix*> kit_fixes() { return {&g_fix_stackdump, &g_fix_cheatwait, &g_fix_reentry, &g_fix_hookkeep}; }

KitFix* kit_fix_find(const char* name)
{
    for (KitFix* f : kit_fixes())
        if (!strcmp(f->name, name))
            return f;
    return nullptr;
}

// ---- per-thread flags ----
static DWORD g_tls = TlsAlloc();

uintptr_t thread_flags()
{
    if (g_tls == TLS_OUT_OF_INDEXES)
        return 0;
    DWORD e = GetLastError(); // TlsGetValue clears it; an exception handler must not change it
    uintptr_t v = (uintptr_t)TlsGetValue(g_tls);
    SetLastError(e);
    return v;
}

void thread_flags_set(uintptr_t f, bool on)
{
    if (g_tls == TLS_OUT_OF_INDEXES)
        return;
    DWORD e = GetLastError();
    uintptr_t v = (uintptr_t)TlsGetValue(g_tls);
    TlsSetValue(g_tls, (void*)(on ? v | f : v & ~f));
    SetLastError(e);
}

// ---- a page whose first access faults and is then repaired ----
// The fault reaches SporeKit's exception logger first (it is the first vectored handler), then this
// handler (the last one) makes the page readable and the instruction runs again. A test touches the
// page while it holds a lock of the fault logging: the logger runs inside that lock.
static void* g_page;

static LONG CALLBACK test_page_handler(EXCEPTION_POINTERS* ep)
{
    auto* er = ep->ExceptionRecord;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || !g_page || er->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t a = er->ExceptionInformation[1], p = (uintptr_t)g_page;
    if (a < p || a >= p + 4096)
        return EXCEPTION_CONTINUE_SEARCH;
    DWORD old;
    VirtualProtect(g_page, 4096, PAGE_READWRITE, &old);
    return EXCEPTION_CONTINUE_EXECUTION;
}

void* test_fault_page()
{
    if (!g_page) {
        g_page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        AddVectoredExceptionHandler(0, test_page_handler);
    }
    DWORD old;
    VirtualProtect(g_page, 4096, PAGE_NOACCESS, &old);
    return g_page;
}

} // namespace sk
