// Hang watchdog: if the main thread makes no progress (boot steps / frames) for a while,
// dump its stack to the log. Uses only calls that do not take the loader lock, so it
// also works when the main thread is stuck inside LoadLibrary/DllMain.
#include "core.h"

#include <atomic>
#include <intrin.h>
#include <cstdio>

namespace sk {

static std::atomic<uint32_t> g_progress{0};
static std::atomic<const char*> g_stage{"boot"};

void watchdog_mark(const char* stage)
{
    g_stage = stage;
    g_progress++;
}

// module name from an address without the loader lock: AllocationBase -> PE export name
static std::string module_of(uintptr_t a, uintptr_t* base_out)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE)
        return {};
    auto* b = (uint8_t*)mbi.AllocationBase;
    *base_out = (uintptr_t)b;
    auto* dos = (IMAGE_DOS_HEADER*)b;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return "?";
    auto* nt = (IMAGE_NT_HEADERS*)(b + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir.VirtualAddress)
        return b == (uint8_t*)GetModuleHandleW(nullptr) ? "SporeApp.exe" : "?";
    auto* exp = (IMAGE_EXPORT_DIRECTORY*)(b + dir.VirtualAddress);
    return (const char*)(b + exp->Name);
}

static void dump_thread_stack(DWORD tid, const char* why);
static void dump_main_stack(const char* why) { dump_thread_stack(main_thread_id(), why); }

// PEB->LoaderLock (RTL_CRITICAL_SECTION*) : OwningThread at +0x0C
static DWORD loader_lock_owner()
{
    uint8_t* peb = (uint8_t*)__readfsdword(0x30);
    auto* cs = *(RTL_CRITICAL_SECTION**)(peb + 0xA0);
    return cs ? (DWORD)(uintptr_t)cs->OwningThread : 0;
}

static void dump_thread_stack(DWORD tid, const char* why)
{
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
    if (!t)
        return;
    SuspendThread(t);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_FULL;
    GetThreadContext(t, &c);
    std::string out;
    char b[256];
    snprintf(b, sizeof b, "WATCHDOG (%s, stage '%s'): thread %lu%s eip=%08lX esp=%08lX\n", why,
             g_stage.load(), tid, tid == main_thread_id() ? " (main)" : "", c.Eip, c.Esp);
    out += b;
    auto describe = [&](uintptr_t a) {
        uintptr_t base = 0;
        std::string m = module_of(a, &base);
        std::string s = symbols_reverse(a);
        snprintf(b, sizeof b, "%s+0x%X %s", m.c_str(), (unsigned)(a - base), s.c_str());
        return std::string(b);
    };
    out += "  eip " + describe(c.Eip) + "\n";
    const uint32_t* sp = (const uint32_t*)c.Esp;
    int found = 0;
    for (int i = 0; i < 2048 && found < 40; i++) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(sp + i, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
            break;
        uint32_t v = sp[i];
        MEMORY_BASIC_INFORMATION vm;
        if (VirtualQuery((void*)(uintptr_t)v, &vm, sizeof vm) && vm.State == MEM_COMMIT &&
            vm.Type == MEM_IMAGE && (vm.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE | PAGE_EXECUTE_READWRITE))) {
            snprintf(b, sizeof b, "  [esp+%04X] %08X ", i * 4, v);
            out += b + describe(v) + "\n";
            found++;
        }
    }
    ResumeThread(t);
    CloseHandle(t);
    logf(&g_kit_mod, SK_ERROR, "%s", out.c_str());
}

static HANDLE g_started;

static DWORD WINAPI watchdog_thread(void*)
{
    SetEvent(g_started);
    uint32_t last = g_progress;
    uint64_t last_frame = frame_index();
    DWORD since = GetTickCount();
    bool dumped = false;
    DWORD ll_owner = 0, ll_since = 0;
    bool ll_dumped = false;
    for (;;) {
        Sleep(1000);
        DWORD o = loader_lock_owner();
        if (o != ll_owner) {
            ll_owner = o;
            ll_since = GetTickCount();
            ll_dumped = false;
        } else if (o && !ll_dumped && GetTickCount() - ll_since > 10000) {
            dump_thread_stack(o, "loader lock held >10s");
            ll_dumped = true;
        }
        uint32_t p = g_progress;
        uint64_t f = frame_index();
        if (p != last || f != last_frame) {
            last = p;
            last_frame = f;
            since = GetTickCount();
            dumped = false;
            continue;
        }
        // frames legitimately stop in menus only when minimized; boot must progress
        DWORD limit = frame_index() == 0 ? 15000 : 30000;
        if (!dumped && GetTickCount() - since > limit) {
            dump_main_stack(frame_index() == 0 ? "no boot progress" : "no frames");
            dumped = true;
            if (frame_index() > 0)
                diag_hang_report("hang: no frames for 30 s while the game window has the focus");
        }
    }
}

void watchdog_start()
{
    g_started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE t = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
    if (t) {
        WaitForSingleObject(g_started, 2000);
        CloseHandle(t);
    }
}

std::string watchdog_dump_now()
{
    dump_main_stack("requested");
    return "dumped to log";
}

} // namespace sk
