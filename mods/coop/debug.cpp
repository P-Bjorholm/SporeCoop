// Debug/RE helper commands for the coop mod (hot-reloadable experiments).
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game.h"
#include "sporekit.h"
#include "sporekit_game.hpp"

static const SkHost* H;
#define SAY(...) H->out_printf(out, __VA_ARGS__)

namespace {
int c_cam(void*, int argc, const char* const* argv, SkOut* out)
{
    using namespace skg;
    uintptr_t mmget = H->sym("App::IGameModeManager::Get");
    void* mm = cdecl_call<void*>(mmget);
    void* viewer = mm ? vcall<void*>(mm, 0x58 / 4) : nullptr;
    if (!viewer) {
        SAY("no viewer");
        return 1;
    }
    float* cam = field<float*>(viewer, 0x170);
    SAY("viewer=%p camera=%p\n", viewer, cam);
    if (!cam || !readable(cam, 0x98))
        return 1;
    for (int r = 0; r < 4; r++)
        SAY("  m[%d] = %8.3f %8.3f %8.3f %8.3f\n", r, cam[r * 4], cam[r * 4 + 1], cam[r * 4 + 2], cam[r * 4 + 3]);
    SAY("viewOffset=(%.3f %.3f) viewWindow=(%.3f %.3f) recip=(%.3f %.3f) proj=%d near=%.3f far=%.1f\n",
        cam[0x54 / 4], cam[0x58 / 4], cam[0x5C / 4], cam[0x60 / 4], cam[0x64 / 4], cam[0x68 / 4],
        *(int*)&cam[0x6C / 4], cam[0x70 / 4], cam[0x74 / 4]);
    uint32_t* vp = (uint32_t*)&cam[0x78 / 4];
    SAY("viewport x=%u y=%u w=%u h=%u\n", vp[0], vp[1], vp[2], vp[3]);
    float* vm = (float*)viewer;
    for (int r = 0; r < 4; r++)
        SAY("  view[%d] = %8.3f %8.3f %8.3f %8.3f\n", r, vm[r * 4], vm[r * 4 + 1], vm[r * 4 + 2], vm[r * 4 + 3]);
    if (argc >= 3) {
        Vector3 pos{}, dir{};
        uintptr_t f = H->sym("App::cViewer::GetCameraToPoint");
        bool ok = thiscall<bool>(f, viewer, (float)atof(argv[1]), (float)atof(argv[2]), &pos, &dir);
        SAY("ray(%s,%s) ok=%d pos=(%.2f %.2f %.2f) dir=(%.3f %.3f %.3f)\n", argv[1], argv[2], ok, pos.x, pos.y,
            pos.z, dir.x, dir.y, dir.z);
    }
    void* av = game::avatar();
    if (av) {
        auto p = game::position(av);
        SAY("avatar=(%.2f %.2f %.2f)\n", p.x, p.y, p.z);
        // camera space with both matrix conventions
        float a[3], b[3];
        for (int i = 0; i < 3; i++) {
            a[i] = cam[i * 4 + 0] * p.x + cam[i * 4 + 1] * p.y + cam[i * 4 + 2] * p.z + cam[i * 4 + 3];
            b[i] = cam[0 * 4 + i] * p.x + cam[1 * 4 + i] * p.y + cam[2 * 4 + i] * p.z + cam[3 * 4 + i];
        }
        SAY("rows: (%.2f %.2f %.2f)  cols: (%.2f %.2f %.2f)\n", a[0], a[1], a[2], b[0], b[1], b[2]);
    }
    return 0;
}
} // namespace
void coop_register_debug_commands(const SkHost* host, SkMod* self)
{
    H = host;
    void coop_register_debug_commands2(const SkHost*, SkMod*);
    coop_register_debug_commands2(host, self);
    host->command_add(self, "coop.cam", "coop.cam [x y] - dump camera, optional screen ray", c_cam, nullptr,
                      SK_CMD_MAIN_THREAD);
}


namespace {
int c_w2s(void*, int, const char* const*, SkOut* out)
{
    void* av = game::avatar();
    if (!av) {
        SAY("no avatar");
        return 1;
    }
    auto p = game::position(av);
    float w[3] = {p.x, p.y, p.z}, sc[2] = {0, 0};
    int ok = H->world_to_screen(w, sc);
    SAY("avatar (%.2f %.2f %.2f) -> ok=%d screen=(%.1f %.1f)\n", p.x, p.y, p.z, ok, sc[0], sc[1]);
    // check with the game's own inverse: ray through that pixel should pass near the avatar
    using namespace skg;
    void* mm = cdecl_call<void*>(H->sym("App::IGameModeManager::Get"));
    void* viewer = vcall<void*>(mm, 0x58 / 4);
    Vector3 o{}, d{};
    thiscall<bool>(H->sym("App::cViewer::GetCameraToPoint"), viewer, sc[0], sc[1], &o, &d);
    float vx = p.x - o.x, vy = p.y - o.y, vz = p.z - o.z;
    float t = vx * d.x + vy * d.y + vz * d.z;
    float cx = o.x + d.x * t - p.x, cy = o.y + d.y * t - p.y, cz = o.z + d.z * t - p.z;
    SAY("ray miss distance %.3f m (t=%.1f)\n", std::sqrt(cx * cx + cy * cy + cz * cz), t);
    return 0;
}
} // namespace

namespace {
int c_dna(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc > 2 && !strcmp(argv[1], "add")) {
        // simulate a gain as the game would report it (goes through the coop hook)
        skg::cdecl_call<void>(H->sym("Simulator::cCreatureGameData::AddEvolutionPoints"), (float)atof(argv[2]));
    }
    SAY("dna=%.1f", game::dna());
    return 0;
}
} // namespace

namespace {
typedef bool(SK_THISCALL* LoadGame_t)(void*, const void*);
LoadGame_t o_LoadGame;
SkMod* g_self;

std::string str16(const void* s)
{
    const char16_t* b = skg::field<const char16_t*>((void*)s, 0);
    const char16_t* e = skg::field<const char16_t*>((void*)s, 4);
    std::string out;
    if (!b || e < b || e - b > 260 || !skg::readable(b, (e - b) * 2 + 2))
        return "<?>";
    for (; b < e; b++)
        out += (*b < 128) ? (char)*b : '?';
    return out;
}

bool SK_THISCALL h_LoadGame(void* self, const void* params)
{
    H->log(g_self, SK_INFO, "LoadGame name='%s' star='%s' f20='%s'", str16(params).c_str(),
           str16((const uint8_t*)params + 0x10).c_str(), str16((const uint8_t*)params + 0x20).c_str());
    const uint32_t* p = (const uint32_t*)params;
    for (int i = 0; i < 0xB4 / 4; i += 8)
        H->log(g_self, SK_INFO, "  +%02X: %08X %08X %08X %08X %08X %08X %08X %08X", i * 4, p[i], p[i + 1], p[i + 2],
               p[i + 3], p[i + 4], p[i + 5], p[i + 6], p[i + 7]);
    return o_LoadGame(self, params);
}

int c_loadworld(void*, int argc, const char* const* argv, SkOut* out)
{
    if (argc < 3) {
        SAY("usage: coop.loadworld <windows slot dir ending with \\> <Planet.spo>");
        return 1;
    }
    auto w = [](const char* s) { std::u16string r; for (; *s; s++) r += (char16_t)(unsigned char)*s; return r; };
    std::u16string dir = w(argv[1]), name = w(argv[2]);
    void* db = skg::cdecl_call<void*>(H->sym("Resource::GetDatabaseByID"), 0x4729A47u);
    if (!db) { SAY("no save DB"); return 1; }
    const char16_t* old = skg::vcall<const char16_t*>(db, 0x28 / 4);
    std::string olds; for (const char16_t* p = old; p && *p; p++) olds += (char)*p;
    int acc = skg::vcall<int>(db, 0x20 / 4);              // GetAccessFlags
    bool closed = skg::vcall<bool>(db, 0x1C / 4);         // Close
    bool ok = skg::vcall<bool>(db, 0x2C / 4, dir.c_str()); // SetLocation
    bool opened = skg::vcall<bool>(db, 0x18 / 4, acc ? acc : 3, 6, 0); // Open(access, CD::Default, autoOpen)
    if (void* sm = skg::cdecl_call<void*>(H->sym("Simulator::cStarManager::Get")))
        skg::thiscall<void>(H->sym("Simulator::cStarManager::ReadStarsFromSaveDB"), sm);
    SAY("access=%d close=%d open=%d\n", acc, closed, opened);
    void* mgr = skg::cdecl_call<void*>(H->sym("Simulator::cGamePersistenceManager::Get"));
    skg::thiscall<void>(H->sym("Simulator::cGamePersistenceManager::LoadGameByName"), mgr, name.c_str());
    SAY("save DB %s -> %s (SetLocation %d), loading %s", olds.c_str(), argv[1], ok, argv[2]);
    return 0;
}

int c_savedb(void*, int, const char* const*, SkOut* out)
{
    void* db = skg::cdecl_call<void*>(H->sym("Resource::GetDatabaseByID"), 0x4729A47u);
    if (!db) { SAY("no save DB"); return 1; }
    const char16_t* loc = skg::vcall<const char16_t*>(db, 0x28 / 4);
    std::string l; for (const char16_t* p = loc; p && *p; p++) l += (char)*p;
    SAY("save DB %p location=%s access=%d refs=%d", db, l.c_str(), skg::vcall<int>(db, 0x20 / 4),
        skg::vcall<int>(db, 0x10 / 4));
    return 0;
}

int c_loadhook(void*, int, const char* const*, SkOut* out)
{
    if (!o_LoadGame)
        H->hook_create(g_self, (void*)H->rebase(0x00B271A0), (void*)h_LoadGame, (void**)&o_LoadGame);
    SAY("LoadGame logging hook installed");
    return 0;
}
} // namespace

void coop_register_debug_commands2(const SkHost* host, SkMod* self)
{
    g_self = self;
    host->command_add(self, "coop.loadworld", "debug: point the save DB to a slot dir and load a planet", c_loadworld,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.savedb", "debug: current save folder database", c_savedb, nullptr,
                      SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.loadhook", "debug: log cGamePersistenceManager::LoadGame parameters", c_loadhook,
                      nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.dna", "coop.dna [add <n>] - shared DNA debug", c_dna, nullptr, SK_CMD_MAIN_THREAD);
    host->command_add(self, "coop.w2s", "check world_to_screen against the game's screen ray", c_w2s, nullptr,
                      SK_CMD_MAIN_THREAD);
}
