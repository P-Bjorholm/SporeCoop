// Exception logger: records access violations and other faults (with nearest symbol)
// before the game's / Wine's handlers swallow them. Invaluable when a hook breaks a frame.
#include "core.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace sk {

struct ExcRec {
    DWORD code;
    uintptr_t eip;
    uintptr_t addr; // faulting data address for AV
    DWORD tid;
    uint32_t count;
    std::string where;
    uint32_t regs[8];
    uint32_t stack[12];
    uint32_t rets[10]; // likely return addresses into the exe found deeper on the stack (0 = none)
};

// a dword on the stack that points just after a call instruction in the exe's code
static bool exe_return_address(uint32_t v)
{
    static uintptr_t lo, hi;
    if (!hi) {
        auto base = (const uint8_t*)GetModuleHandleW(nullptr);
        auto nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        lo = (uintptr_t)base + sec->VirtualAddress;
        hi = lo + sec->Misc.VirtualSize;
    }
    if (v < lo + 6 || v >= hi)
        return false;
    const uint8_t* c = (const uint8_t*)v;
    return c[-5] == 0xE8 || (c[-6] == 0xFF && (c[-5] & 0x38) == 0x10) || (c[-2] == 0xFF && (c[-1] & 0xF8) == 0xD0) ||
           (c[-3] == 0xFF && (c[-2] & 0x38) == 0x10) || (c[-7] == 0xFF && (c[-6] & 0x38) == 0x10);
}

static std::mutex g_mu;
static std::vector<ExcRec> g_recs; // unique by (code, eip)
static std::atomic<uint32_t> g_total{0};

static LONG CALLBACK veh(EXCEPTION_POINTERS* ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    // ignore debugger/C++/thread-naming noise
    if (code == 0x406D1388 || code == 0xE06D7363 || code == DBG_PRINTEXCEPTION_C ||
        code == 0x4001000A || code == STATUS_BREAKPOINT || code == STATUS_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_PRIV_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    g_total++;
    CONTEXT* c = ep->ContextRecord;
    uintptr_t eip = c->Eip;
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& r : g_recs)
        if (r.code == code && r.eip == eip) {
            r.count++;
            return EXCEPTION_CONTINUE_SEARCH;
        }
    if (g_recs.size() >= 64)
        return EXCEPTION_CONTINUE_SEARCH;
    ExcRec r{};
    r.code = code;
    r.eip = eip;
    r.addr = code == EXCEPTION_ACCESS_VIOLATION ? ep->ExceptionRecord->ExceptionInformation[1] : 0;
    r.tid = GetCurrentThreadId();
    r.count = 1;
    r.where = symbols_reverse(eip);
    if (r.where.empty()) {
        HMODULE m = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)eip, &m) && m) {
            wchar_t n[MAX_PATH];
            GetModuleFileNameW(m, n, MAX_PATH);
            std::wstring s = n;
            char off[32];
            snprintf(off, sizeof off, "+0x%X", (unsigned)(eip - (uintptr_t)m));
            r.where = narrow(s.substr(s.find_last_of(L"\\/") + 1)) + off;
        }
    }
    uint32_t regs[8] = {c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp};
    memcpy(r.regs, regs, sizeof regs);
    const uint32_t* sp = (const uint32_t*)c->Esp;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(sp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT)
        for (int i = 0; i < 12; i++)
            r.stack[i] = sp[i];
    // the call chain (roughly): return addresses within 2 KB of the stack, same committed region
    if (VirtualQuery(sp, &mbi, sizeof mbi) && mbi.State == MEM_COMMIT) {
        const uint32_t* end = (const uint32_t*)((const uint8_t*)mbi.BaseAddress + mbi.RegionSize);
        int n = 0;
        for (const uint32_t* q = sp; q < end && q < sp + 512 && n < 10; q++)
            if (exe_return_address(*q))
                r.rets[n++] = *q;
    }
    g_recs.push_back(r);
    // logging takes the log mutex only; safe enough for a first-chance handler
    logf(&g_kit_mod, SK_ERROR,
         "exception 0x%08lX at 0x%08X %s (data 0x%08X) tid=%lu eax=%08X ebx=%08X ecx=%08X edx=%08X "
         "esi=%08X edi=%08X ebp=%08X esp=%08X",
         code, (unsigned)eip, r.where.c_str(), (unsigned)r.addr, r.tid, regs[0], regs[1], regs[2],
         regs[3], regs[4], regs[5], regs[6], regs[7]);
    if (r.rets[0])
        logf(&g_kit_mod, SK_ERROR, "  calls: %08X %08X %08X %08X %08X %08X %08X %08X %08X %08X", r.rets[0], r.rets[1],
             r.rets[2], r.rets[3], r.rets[4], r.rets[5], r.rets[6], r.rets[7], r.rets[8], r.rets[9]);
    return EXCEPTION_CONTINUE_SEARCH;
}

void exceptions_install()
{
    AddVectoredExceptionHandler(1, veh);
}

std::string exceptions_report()
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::string s = "total faults: " + std::to_string(g_total.load()) + "\n";
    for (auto& r : g_recs) {
        char b[512];
        snprintf(b, sizeof b, "0x%08lX x%u at 0x%08X %s data=0x%08X tid=%lu%s\n  stack:", r.code, r.count,
                 (unsigned)r.eip, r.where.c_str(), (unsigned)r.addr, r.tid, r.tid == main_thread_id() ? " (main)" : "");
        s += b;
        for (int i = 0; i < 12; i++) {
            snprintf(b, sizeof b, " %08X", r.stack[i]);
            s += b;
        }
        if (r.rets[0]) {
            s += "\n  calls:";
            for (int i = 0; i < 10 && r.rets[i]; i++) {
                snprintf(b, sizeof b, " %08X", r.rets[i]);
                s += b;
            }
        }
        s += "\n";
    }
    return s;
}

void exceptions_clear()
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_recs.clear();
    g_total = 0;
}

} // namespace sk
