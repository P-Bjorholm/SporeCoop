#include "gamefixes.h"

#include <windows.h>

#include <cstring>
#include <string>

namespace gamefixes {

static const SkHost* H;
static SkMod* M;

// A planet view of the galaxy menu reads the temperature of a terrain state that is null while a
// save loads under it: `call cTerrainStateMgr_GetTemperatureScore` (0xFB89D0: fld [ecx+0x30]; ret)
// at 0xF9E5EC faulted with ecx 0 (first-chance AV, mostly survived; seen on guests loading a received
// world, more often with 3-4 players rejoining). The call goes to this guard instead: 0.0 for a null
// state, else the same load.
extern "C" __attribute__((naked)) void sporecoop_temperature_guard()
{
    asm volatile("test %ecx, %ecx\n\t"
                 "jz 1f\n\t"
                 "flds 0x30(%ecx)\n\t"
                 "ret\n"
                 "1:\n\t"
                 "fldz\n\t"
                 "ret\n\t");
}

struct CallPatch {
    uintptr_t at;     // the call instruction (E8 rel32)
    uintptr_t target; // its original target
    uint8_t saved[5];
    bool on = false;
};
static CallPatch g_temp{0xF9E5EC, 0xFB89D0};
static unsigned g_patched;

static bool redirect(CallPatch& p, void* to)
{
    uint8_t* code = (uint8_t*)p.at;
    if (IsBadReadPtr(code, 5) || code[0] != 0xE8)
        return false;
    int32_t rel;
    memcpy(&rel, code + 1, 4);
    if (p.at + 5 + rel != p.target)
        return false; // not the build we know: leave it alone
    DWORD old;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(p.saved, code, 5);
    int32_t nrel = (int32_t)((uintptr_t)to - (p.at + 5));
    memcpy(code + 1, &nrel, 4);
    VirtualProtect(code, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    p.on = true;
    return true;
}

static void restore(CallPatch& p)
{
    if (!p.on)
        return;
    DWORD old;
    if (VirtualProtect((void*)p.at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        memcpy((void*)p.at, p.saved, 5);
        VirtualProtect((void*)p.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void*)p.at, 5);
    }
    p.on = false;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    g_patched = redirect(g_temp, (void*)sporecoop_temperature_guard);
    H->log(M, SK_INFO, "game fixes: planet-view temperature guard %s", g_patched ? "on" : "off (unknown code)");
}

void shutdown() { restore(g_temp); }

std::string status() { return std::string("game fixes: temperature guard ") + (g_temp.on ? "on" : "off") + "\n"; }

} // namespace gamefixes
