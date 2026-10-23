#include "gamefixes.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace gamefixes {

static const SkHost* H;
static SkMod* M;

// The texture job of a terrain sphere (docs/re/space.md 8.3): a one-shot render layer that the
// sphere queues in the render manager to draw its planet textures face by face (pass +0x10 0..3,
// cube face +0x14 0..5). Its DrawLayer (vtable slot 3, thiscall, 4 arguments, ret 0x10) reads the
// sphere (+0xC) without checks: the terrain state (sphere+0x20C, the sphere's vt 0x10) and the face
// render targets (sphere+0x148/+0x160 [face]). While a save loaded from the galaxy menu the game drew
// such a job for a sphere whose state and targets were already gone: first-chance AVs at 0xFB89D0
// (cTerrainStateMgr_GetTemperatureScore, ecx 0) and 0xF9E660 (the face target). The job's vtable
// slot goes to this guard, which skips the draw (as the game's own early return when +0x20 is
// null) when anything it reads is missing.
using DrawLayerFn = void(__thiscall*)(void*, int, int, void*, void*);
static DrawLayerFn g_draw_orig;
static uintptr_t* g_draw_slot;
static std::atomic<unsigned> g_skipped{0};
static std::string g_last; // the last skipped job (main thread only)

template <class T> static T at(const void* p, size_t off) { return *(const T*)((const uint8_t*)p + off); }

static bool sphere_ready(const void* job, const uint8_t* sphere)
{
    if (!sphere || !at<void*>(sphere, 0x20C))
        return false;
    int pass = at<int>(job, 0x10), face = at<int>(job, 0x14);
    if (pass >= 0 && pass <= 2 && face >= 0 && face < 6) {
        if (!at<void*>(sphere, 0x160 + 4 * face))
            return false;
        if (pass == 2 && !at<void*>(sphere, 0x148 + 4 * face))
            return false;
    }
    return true;
}

static void __fastcall draw_layer_guard(void* job, void*, int flags, int index, void* viewers, void* engine)
{
    const uint8_t* sphere = at<const uint8_t*>(job, 0xC);
    if (!at<void*>(job, 0x20) || sphere_ready(job, sphere)) {
        g_draw_orig(job, flags, index, viewers, engine);
        return;
    }
    if (sphere)
        *(int*)(sphere + 0x8A4) = -1; // what DrawLayer does first: this job is no longer queued
    unsigned n = ++g_skipped;
    if (n > 8)
        return;
    char b[320];
    snprintf(b, sizeof b,
             "job %p sphere %p pass %d face %d state %p targets %p/%p sphere job %p id %d tid %lu",
             job, sphere, at<int>(job, 0x10), at<int>(job, 0x14), sphere ? at<void*>(sphere, 0x20C) : nullptr,
             sphere ? at<void*>(sphere, 0x148 + 4 * (at<int>(job, 0x14) & 7)) : nullptr,
             sphere ? at<void*>(sphere, 0x160 + 4 * (at<int>(job, 0x14) & 7)) : nullptr,
             sphere ? at<void*>(sphere, 0x8A0) : nullptr, sphere ? at<int>(sphere, 0x8A4) : 0, GetCurrentThreadId());
    g_last = b;
    H->log(M, SK_WARN, "game fixes: skipped a planet texture job of a terrain sphere without terrain (%s)", b);
}

static bool patch_slot(uintptr_t* slot, uintptr_t value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return false;
    *slot = value;
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
}

void init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    uintptr_t vt = H->sym("Terrain::cTerrainSphereTextureJob::vftable");
    uintptr_t fn = H->sym("Terrain::cTerrainSphereTextureJob::DrawLayer");
    uintptr_t* slot = vt ? (uintptr_t*)(vt + 3 * 4) : nullptr;
    bool ok = slot && fn && !IsBadReadPtr(slot, 4) && *slot == fn && patch_slot(slot, (uintptr_t)&draw_layer_guard);
    if (ok) {
        g_draw_slot = slot;
        g_draw_orig = (DrawLayerFn)fn;
    }
    H->log(M, SK_INFO, "game fixes: planet texture job guard %s", ok ? "on" : "off (unknown code)");
}

void shutdown()
{
    if (g_draw_slot && patch_slot(g_draw_slot, (uintptr_t)g_draw_orig))
        g_draw_slot = nullptr;
}

std::string status()
{
    return std::string("game fixes: planet texture job guard ") + (g_draw_slot ? "on" : "off") + ", skipped " +
           std::to_string(g_skipped.load()) + (g_last.empty() ? "" : " (last: " + g_last + ")") + "\n";
}

} // namespace gamefixes
