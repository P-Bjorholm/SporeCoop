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
};

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
    g_recs.push_back(r);
    // logging takes the log mutex only; safe enough for a first-chance handler
    logf(&g_kit_mod, SK_ERROR,
         "exception 0x%08lX at 0x%08X %s (data 0x%08X) tid=%lu eax=%08X ebx=%08X ecx=%08X edx=%08X "
         "esi=%08X edi=%08X ebp=%08X esp=%08X",
         code, (unsigned)eip, r.where.c_str(), (unsigned)r.addr, r.tid, regs[0], regs[1], regs[2],
         regs[3], regs[4], regs[5], regs[6], regs[7]);
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
        snprintf(b, sizeof b, "0x%08lX x%u at 0x%08X %s data=0x%08X tid=%lu\n  stack:", r.code, r.count,
                 (unsigned)r.eip, r.where.c_str(), (unsigned)r.addr, r.tid);
        s += b;
        for (int i = 0; i < 12; i++) {
            snprintf(b, sizeof b, " %08X", r.stack[i]);
            s += b;
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
