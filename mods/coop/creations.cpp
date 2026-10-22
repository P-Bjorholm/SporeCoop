#include "creations.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"

namespace creations {

using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

static const SkHost* H;
static SkMod* M;
static Ctx C;

struct KindInfo {
    const char* name;
    uint32_t type, group;
};
static const KindInfo kInfo[kKinds] = {
    {"creature", 0x2B978C46, 0x40626200}, // crt
    {"cell", 0x3D97A8E4, 0x40616200},     // cll
    {"ufo", 0x476A98C7, 0x40656200},      // ufo
};
constexpr uint32_t kBem = 0x1A99B06B, kMeta = 0x030BDEE3;
constexpr uint32_t kEditorSavesArea = 0x11AC199;
constexpr int kPrioPlayer = 1000, kPrioWorld = 2000; // EditorSaves is 1000; the list is searched highest first

static uintptr_t a_GetDb, a_OpNew, a_DbCtor, a_RmPtr, a_SetSpecies, a_GetEmpire, a_CellPreload;

// ---- own creations (My Creations = EditorSaves.package) ----
static std::wstring g_es_path, g_dir; // EditorSaves.package and our folder next to it
static std::vector<Option> g_opts[kKinds];
static dbpf::Package g_es;  // My Creations, read every few seconds
static bool g_es_ok;
static double g_opts_read = -100;
static Key g_choice[kKinds];  // explicit choice ({} = default)
static bool g_chosen[kKinds]; // the player chose (even "none")

// ---- bundles: a creation with its records ----
struct Bundle {
    Key key;                         // the creation (crt/cll/ufo key)
    std::vector<dbpf::Record> recs;  // bem + metadata, empty for a premade (everybody has it)
};
static std::map<std::pair<uint8_t, uint8_t>, std::vector<uint8_t>> g_store; // host: (owner, kind) -> bundle
static uint32_t g_sent_id[kKinds];                                             // what we sent of our own
static double g_last_check;
struct Rx {
    uint32_t id = 0, total = 0;
    std::vector<uint8_t> data;
    std::vector<bool> got;
    uint32_t have = 0;
};
static std::map<std::pair<uint8_t, uint8_t>, Rx> g_rx;
struct Applied {
    uint32_t id = 0;
    Key key;  // usable here (remapped instance)
    Key orig; // the owner's own key
    void* db = nullptr;
    std::string name;
};
static std::map<std::pair<uint8_t, uint8_t>, Applied> g_applied; // (owner, kind)
static unsigned g_bundles_in, g_bundles_out, g_registered, g_swaps;
static std::string g_last_error;

// ---- helpers ----
static uint32_t fnv(const void* p, size_t n, uint32_t h = 0x811C9DC5)
{
    for (size_t i = 0; i < n; i++)
        h = (h * 0x1000193) ^ ((const uint8_t*)p)[i];
    return h;
}

static bool read_file(const std::wstring& path, std::vector<uint8_t>& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0;
    bool ok = sz.QuadPart < (256 << 20) && ReadFile(f, out.data(), (DWORD)out.size(), &rd, nullptr) && rd == out.size();
    CloseHandle(f);
    return ok;
}

static bool write_file(const std::wstring& path, const std::vector<uint8_t>& data)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD wr = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &wr, nullptr) && wr == data.size();
    CloseHandle(f);
    return ok;
}

static std::string hex(uint32_t v)
{
    char b[16];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

// where My Creations live: the game's EditorSaves database location
static bool find_paths()
{
    if (!g_es_path.empty())
        return true;
    void* db = a_GetDb ? cdecl_call<void*>(a_GetDb, kEditorSavesArea) : nullptr;
    if (!db)
        return false;
    const wchar_t* loc = vcall<const wchar_t*>(db, 0x28 / 4); // GetLocation
    if (!loc || IsBadReadPtr(loc, 8) || !(loc[1] == L':' || loc[0] == L'\\'))
        return false;
    std::wstring p = loc;
    if (p.size() < 10 || p.size() > 1000)
        return false;
    g_es_path = p;
    g_dir = p.substr(0, p.find_last_of(L"\\/")) + L"\\SporeCoop";
    CreateDirectoryW(g_dir.c_str(), nullptr);
    // the choices belong to this profile (creation ids are per profile): SporeCoop\choices.ini
    for (int k = 0; k < kKinds; k++) {
        wchar_t buf[32] = {};
        GetPrivateProfileStringW(L"creations", std::wstring(kInfo[k].name, kInfo[k].name + strlen(kInfo[k].name)).c_str(),
                                 L"-", buf, 32, (g_dir + L"\\choices.ini").c_str());
        if (buf[0] != L'-') {
            g_chosen[k] = true;
            uint32_t inst = (uint32_t)wcstoul(buf, nullptr, 16);
            g_choice[k] = inst ? Key{inst, kInfo[k].type, kInfo[k].group} : Key{};
        }
    }
    H->log(M, SK_INFO, "creations: My Creations %ls", g_es_path.c_str());
    return true;
}

static bool load_editor_saves(dbpf::Package& pk)
{
    std::vector<uint8_t> f;
    return find_paths() && read_file(g_es_path, f) && pk.load(f);
}

static void refresh_options(double now)
{
    if (now - g_opts_read < 10)
        return;
    g_opts_read = now;
    g_es_ok = load_editor_saves(g_es);
    if (!g_es_ok)
        return;
    const dbpf::Package& pk = g_es;
    for (int k = 0; k < kKinds; k++) {
        g_opts[k].clear();
        for (auto& e : pk.entries()) {
            if (e.key.type != kBem || e.key.group != kInfo[k].group)
                continue;
            Option o;
            o.key = {e.key.instance, kInfo[k].type, kInfo[k].group};
            std::vector<uint8_t> m;
            std::string author;
            if (const dbpf::Entry* me = pk.find({e.key.instance, kMeta, e.key.group}))
                if (pk.read(*me, m))
                    dbpf::metadata_names(m, author, o.name);
            if (o.name.empty())
                o.name = hex(o.key.instance);
            g_opts[k].push_back(o);
        }
        // creation ids are handed out in order: the highest is the newest
        std::sort(g_opts[k].begin(), g_opts[k].end(),
                  [](const Option& a, const Option& b) { return a.key.instance > b.key.instance; });
    }
}

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }

static uint8_t* cell_S()
{
    uint8_t* g = game::mode_id() == modes::kCell ? modes::cell_game() : nullptr;
    return g ? *(uint8_t**)(g + 0x5190) : nullptr; // cCellSerializableData
}

static Key cell_key()
{
    uint8_t* s = cell_S();
    if (!s)
        return {};
    auto* k = (uint32_t*)(s + 0x10); // the player's cell model
    return {k[0], k[1], k[2]};
}

static Key ufo_key()
{
    void* emp = game::mode_id() == modes::kSpace && a_GetEmpire ? cdecl_call<void*>(a_GetEmpire) : nullptr;
    if (!emp)
        return {};
    auto* k = (uint32_t*)((uint8_t*)emp + 0xB8); // cEmpire::mUFOKey
    return {k[0], k[1], k[2]};
}

// a key some registered database has (RM FindRecord)
static bool key_exists(const Key& k)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
    return rm && vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) != nullptr;
}

static Key avatar_species_key()
{
    void* av = game::mode_id() == modes::kCreature ? game::avatar() : nullptr;
    if (!av)
        return {};
    auto* k = (uint32_t*)((uint8_t*)av + 0xB28);
    return {k[0], k[1], k[2]};
}

Option mine(Kind k)
{
    if (g_chosen[k] && k != kUfo) { // the ship is always the own empire's
        for (auto& o : g_opts[k])
            if (o.key == g_choice[k])
                return o;
        return {g_choice[k], g_choice[k].instance ? hex(g_choice[k].instance) : ""};
    }
    auto named = [&](const Key& a) -> Option {
        for (auto& o : g_opts[k])
            if (o.key.instance == a.instance)
                return o;
        return {a, ""};
    };
    // a spaceship: everybody plays its own galaxy, the ship of its own empire
    if (k == kUfo) {
        Key u = ufo_key();
        return u.instance ? named(u) : Option{};
    }
    // default: a guest plays its newest creation; the host keeps the one of its world
    if (C.sess && C.sess->role() == net::Role::Client && !g_opts[k].empty())
        return g_opts[k].front();
    Key a = k == kCreature ? avatar_species_key() : cell_key();
    if (a.instance)
        return named(a);
    return {};
}

// ---- bundle serialization ----
static std::vector<uint8_t> pack(const Bundle& b)
{
    net::Writer w;
    w.u32(b.key.instance); w.u32(b.key.type); w.u32(b.key.group);
    w.u16((uint16_t)b.recs.size());
    for (auto& r : b.recs) {
        w.u32(r.key.type); w.u32(r.key.group); w.u32(r.key.instance);
        w.u32((uint32_t)r.data.size());
        for (uint8_t c : r.data)
            w.u8(c);
    }
    return w.data;
}

static bool unpack(const std::vector<uint8_t>& d, Bundle& b)
{
    net::Reader r(d.data(), d.size());
    b.key.instance = r.u32(); b.key.type = r.u32(); b.key.group = r.u32();
    uint16_t n = r.u16();
    for (int i = 0; i < n && r.ok(); i++) {
        dbpf::Record rec;
        rec.key.type = r.u32(); rec.key.group = r.u32(); rec.key.instance = r.u32();
        uint32_t sz = r.u32();
        if (!r.ok() || sz > (4u << 20))
            return false;
        rec.data.resize(sz);
        for (uint32_t j = 0; j < sz && r.ok(); j++)
            rec.data[j] = r.u8();
        b.recs.push_back(std::move(rec));
    }
    return r.ok();
}

static bool make_bundle(Kind k, const Key& key, Bundle& b)
{
    b.key = key;
    if (!key.instance || !g_es_ok)
        return key.instance != 0; // a premade: no records (everybody has it)
    const dbpf::Package& pk = g_es;
    for (auto& e : pk.entries()) {
        if (e.key.instance != key.instance || (e.key.type != kBem && e.key.type != kMeta))
            continue;
        if (e.key.group != kInfo[k].group)
            continue;
        dbpf::Record r;
        r.key = e.key;
        if (pk.read(e, r.data))
            b.recs.push_back(std::move(r));
    }
    return true;
}

// ---- registering a package with the game's resource manager (docs/re/creations.md §3) ----
static void* register_package(const std::wstring& path, int priority)
{
    if (!a_OpNew || !a_DbCtor || !a_RmPtr)
        return nullptr;
    void* rm = *(void**)a_RmPtr;
    if (!rm)
        return nullptr;
    void* db = cdecl_call<void*>(a_OpNew, 0x388u, "SporeCoop creation", 0, 0, 0, 0);
    if (!db)
        return nullptr;
    thiscall<void*>(a_DbCtor, db, (const wchar_t*)path.c_str(), (void*)nullptr);
    vcall<int>((uint8_t*)db + 4, 0);   // AddRef
    vcall<bool>(db, 0x04 / 4);          // Initialize
    if (!vcall<bool>(db, 0x18 / 4, 1, 6, false)) { // Open(read, default, not lazy)
        g_last_error = "Open failed";
        return nullptr;
    }
    if (!vcall<bool>(rm, 0x50 / 4, true, db, priority)) { // RegisterDatabase
        g_last_error = "RegisterDatabase failed";
        return nullptr;
    }
    g_registered++;
    return db;
}

static void unregister_package(void* db)
{
    void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
    if (rm && db)
        vcall<bool>(rm, 0x50 / 4, false, db, 0);
}

// a bundle arrived (or is our own): store it here under a collision-free instance and register it
static void apply(uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data)
{
    Bundle b;
    if (!unpack(data, b)) {
        g_last_error = "bad bundle";
        return;
    }
    auto& ap = g_applied[{owner, (uint8_t)k}];
    if (ap.id == id && ap.key.instance)
        return;
    std::string name;
    for (auto& r : b.recs)
        if (r.key.type == kMeta) {
            std::string author;
            dbpf::metadata_names(r.data, author, name);
        }
    Key use = b.key;
    void* old = ap.db;
    void* db = nullptr;
    if (!b.recs.empty() && find_paths()) {
        // the same id everywhere: owner + content hash (a new version after an editor = a new key)
        uint8_t seed[5] = {owner, (uint8_t)id, (uint8_t)(id >> 8), (uint8_t)(id >> 16), (uint8_t)(id >> 24)};
        uint32_t inst = 0x7C000000u | (fnv(seed, 5) & 0x00FFFFFFu);
        std::vector<dbpf::Record> recs = b.recs;
        for (auto& r : recs)
            if (r.key.instance == b.key.instance)
                r.key.instance = inst;
        // the host's own creation is also its world's species: register it under the original
        // key too, above everybody's own My Creations (a guest may own another creation there)
        if (owner == 0 && C.sess && C.sess->role() == net::Role::Client) {
            std::vector<dbpf::Record> world = b.recs;
            std::wstring wp = g_dir + L"\\world_" + std::to_wstring(k) + L"_" + std::to_wstring(id) + L".package";
            if (write_file(wp, dbpf::write(world)))
                register_package(wp, kPrioWorld);
        }
        std::wstring p = g_dir + L"\\p" + std::to_wstring(owner) + L"_" + std::to_wstring(k) + L"_" +
                         std::to_wstring(id) + L".package";
        if (!write_file(p, dbpf::write(recs))) {
            g_last_error = "cannot write the package";
            return;
        }
        db = register_package(p, kPrioPlayer);
        if (!db)
            return;
        use.instance = inst;
    }
    if (old)
        unregister_package(old);
    ap.id = id;
    ap.key = use;
    ap.orig = b.key;
    ap.db = db;
    ap.name = name;
    g_bundles_in++;
    H->log(M, SK_INFO, "creations: player %u %s = %08X/%08X/%08X '%s' (%u records)", owner, kInfo[k].name,
           use.instance, use.type, use.group, name.c_str(), (unsigned)b.recs.size());
    if (C.appearance_changed)
        C.appearance_changed(owner);
}

// ---- sending ----
constexpr size_t kChunk = 900;

static void send_bundle(int to, uint8_t owner, Kind k, uint32_t id, const std::vector<uint8_t>& data)
{
    for (size_t off = 0; off < data.size() || off == 0; off += kChunk) {
        net::Writer w;
        w.u8(owner);
        w.u8((uint8_t)k);
        w.u32(id);
        w.u32((uint32_t)data.size());
        w.u32((uint32_t)off);
        size_t n = std::min(kChunk, data.size() - off);
        w.u16((uint16_t)n);
        for (size_t i = 0; i < n; i++)
            w.u8(data[off + i]);
        if (to < 0)
            C.sess->broadcast_reliable(net::MSG_CREATION, w);
        else
            C.sess->send_reliable_to((uint8_t)to, net::MSG_CREATION, w);
        if (data.empty())
            break;
    }
}

static void share_own(double now)
{
    if (!C.sess || !C.sess->connected())
        return;
    refresh_options(now);
    uint8_t me = C.sess->my_id();
    for (int k = 0; k < kKinds; k++) {
        Option o = mine((Kind)k);
        if (!o.key.instance)
            continue;
        Bundle b;
        if (!make_bundle((Kind)k, o.key, b))
            continue;
        std::vector<uint8_t> data = pack(b);
        uint32_t id = fnv(data.data(), data.size());
        if (g_sent_id[k] == id)
            continue;
        g_sent_id[k] = id;
        apply(me, (Kind)k, id, data); // our own, under the same key everybody uses
        if (is_host()) {
            g_store[{me, (uint8_t)k}] = data;
            send_bundle(-1, me, (Kind)k, id, data);
        } else
            send_bundle(0, me, (Kind)k, id, data);
        g_bundles_out++;
    }
}

// ---- the own avatar takes the own creature ----
static void* g_swapped_avatar;
static Key g_swapped_key;
static void* g_seen_avatar; // a new avatar (a world was loaded): wait until the world has settled
static double g_seen_since;

// the key this machine uses for its own creation of a kind
static Key own_key_here(Kind kind)
{
    // the own key itself (My Creations has it, and a saved game keeps a key every launch knows);
    // a guest whose own key is also the host's (another creation with the same id: ids are per
    // profile) uses the shared remapped one: the host's world package shadows the own one
    uint8_t me = C.sess->my_id();
    Key k = mine(kind).key;
    auto host = g_applied.find({0, (uint8_t)kind});
    if (me != 0 && host != g_applied.end() && host->second.orig == k)
        k = player_key(me, kind);
    return k;
}

// the own cell: the player's cell model, then the level is rebuilt (the cell hatches again)
static Key g_cell_set;
static bool g_cell_rebuilding;
static void apply_own_cell()
{
    uint8_t* s = cell_S();
    uint8_t* g = modes::cell_game();
    if (!s || !g)
        return;
    if (g_cell_rebuilding) { // wait for the new avatar with the new model, then join the partner
        uint8_t* av = modes::cell_at(*(uint32_t*)(g + 0x411C));
        if (av && *(uint32_t*)(av + 0xFC) == g_cell_set.instance && *(uint8_t*)(g + 0x51D8) == 0) {
            g_cell_rebuilding = false;
            if (C.cell_rebuilt)
                C.cell_rebuilt();
        }
        return;
    }
    Key k = own_key_here(kCell);
    if (!k.instance || k == cell_key() || k == g_cell_set || !key_exists(k))
        return;
    g_cell_set = k;
    auto* d = (uint32_t*)(s + 0x10);
    d[0] = k.instance; d[1] = k.type; d[2] = k.group;
    if (a_CellPreload)
        cdecl_call<void>(a_CellPreload, k.instance, k.type, k.group); // preload set (as the editor does)
    *(uint8_t*)(g + 0x51D8) = 1; // RebuildLevel on the next cell frame
    g_cell_rebuilding = true;
    g_swaps++;
    H->log(M, SK_INFO, "creations: own cell is now %08X (level rebuilt)", k.instance);
}

static void apply_own_avatar()
{
    if (game::mode_id() == modes::kCell && C.sess && C.sess->connected()) {
        apply_own_cell();
        return;
    }
    if (game::mode_id() != modes::kCreature || !C.sess || !C.sess->connected() || !a_SetSpecies)
        return;
    void* av = game::avatar();
    Key k = own_key_here(kCreature);
    if (!av || !k.instance)
        return;
    // SetSpecies while a world is still loading crashed (a null call inside the game): only on an
    // avatar that has lived for a few seconds
    double now = H->time_seconds();
    if (av != g_seen_avatar) {
        g_seen_avatar = av;
        g_seen_since = now;
    }
    if (now - g_seen_since < 3.0)
        return;
    if (av == g_swapped_avatar && k == g_swapped_key)
        return;
    Key cur = avatar_species_key();
    if (!(cur == k)) {
        void* prof = game::species_by_key({k.instance, k.type, k.group});
        if (!prof)
            return; // not resolvable (yet)
        int age = *(int*)((uint8_t*)av + 0xB34);
        thiscall<bool>(a_SetSpecies, av, prof, age, true);
        g_swaps++;
        H->log(M, SK_INFO, "creations: own avatar is now %08X", k.instance);
    }
    g_swapped_avatar = av;
    g_swapped_key = k;
}

// ---- api ----
bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    a_GetDb = H->sym("Resource::GetDatabaseByID");
    a_OpNew = H->sym("Memory::operator_new_named");
    a_DbCtor = H->sym("Resource::DatabasePackedFile::ctor");
    a_RmPtr = H->sym("Resource::ResourceManager_ptr");
    a_SetSpecies = H->sym("Simulator::cCreatureAnimal::SetSpecies");
    a_GetEmpire = H->sym("Simulator::GetPlayerEmpire");
    a_CellPreload = H->sym("Simulator::Cell::PreloadModel");
    return a_GetDb && a_OpNew && a_DbCtor && a_RmPtr;
}

// back from an editor: the edited creature (a new creation in My Creations) is the own one now
// (the avatar appears a moment after the mode changes: wait for it)
static uint32_t g_prev_mode;
static double g_adopt_until;
static bool adopt_after_editor(double now)
{
    uint32_t m = game::mode_id();
    if ((g_prev_mode == modes::kEdit || g_prev_mode == modes::kEditor) && m == modes::kCreature)
        g_adopt_until = now + 30;
    g_prev_mode = m;
    if (now > g_adopt_until)
        return false;
    Key a = avatar_species_key();
    if (!a.instance)
        return true; // not yet: do not apply the old one meanwhile
    g_adopt_until = 0;
    if (!(a == mine(kCreature).key)) {
        g_opts_read = -100;
        refresh_options(now);
        choose(kCreature, a);
        H->log(M, SK_INFO, "creations: back from the editor, own creature %08X", a.instance);
    }
    return false;
}

void tick(double now)
{
    if (!C.sess || !C.sess->connected())
        return;
    if (adopt_after_editor(now))
        return;
    if (now - g_last_check > 2.0) {
        g_last_check = now;
        share_own(now);
    }
    apply_own_avatar();
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_CREATION)
        return;
    uint8_t owner = r.u8(), kind = r.u8();
    uint32_t id = r.u32(), total = r.u32(), off = r.u32();
    uint16_t n = r.u16();
    if (!r.ok() || kind >= kKinds || total > (8u << 20) || off + n > total)
        return;
    if (!is_host() && from != 0)
        return;
    if (is_host() && owner != from)
        return; // a guest only sends its own
    auto& rx = g_rx[{owner, kind}];
    if (rx.id != id) {
        rx = Rx{};
        rx.id = id;
        rx.total = total;
        rx.data.assign(total, 0);
        rx.got.assign((total + kChunk - 1) / kChunk + 1, false);
    }
    size_t ci = off / kChunk;
    if (ci < rx.got.size() && !rx.got[ci]) {
        for (uint16_t i = 0; i < n && r.ok(); i++)
            rx.data[off + i] = r.u8();
        rx.got[ci] = true;
        rx.have += n;
    }
    if (rx.have < rx.total && rx.total > 0)
        return;
    std::vector<uint8_t> data = std::move(rx.data);
    g_rx.erase({owner, kind});
    if (is_host()) { // keep it for players joining later and pass it on
        g_store[{owner, kind}] = data;
        for (auto& p : C.sess->players())
            if (p.id != 0 && p.id != owner)
                send_bundle(p.id, owner, (Kind)kind, id, data);
    }
    apply(owner, (Kind)kind, id, data);
}

void on_player_join(uint8_t id)
{
    if (!is_host())
        return;
    for (auto& [ok, data] : g_store)
        if (ok.first != id)
            send_bundle(id, ok.first, (Kind)ok.second, fnv(data.data(), data.size()), data);
}

void on_session_end()
{
    g_store.clear();
    g_rx.clear();
    for (auto& s : g_sent_id)
        s = 0;
    // registered packages stay (cheap); keys of other players are forgotten
    for (auto it = g_applied.begin(); it != g_applied.end();)
        it = g_applied.erase(it);
    g_swapped_avatar = nullptr;
    g_cell_set = {};
}

Key player_key(uint8_t player, Kind k)
{
    auto it = g_applied.find({player, (uint8_t)k});
    return it == g_applied.end() ? Key{} : it->second.key;
}

void* player_species(uint8_t player)
{
    Key k = player_key(player, kCreature);
    return k.instance ? game::species_by_key({k.instance, k.type, k.group}) : nullptr;
}

std::vector<Option> options(Kind k)
{
    refresh_options(H->time_seconds());
    return g_opts[k];
}

void choose(Kind k, const Key& key)
{
    g_chosen[k] = true;
    g_choice[k] = key;
    if (find_paths()) {
        std::wstring v = key.instance ? std::wstring(L"0x") + std::to_wstring(0) : L"0";
        wchar_t hx[16];
        swprintf(hx, 16, L"%08X", key.instance);
        WritePrivateProfileStringW(L"creations", std::wstring(kInfo[k].name, kInfo[k].name + strlen(kInfo[k].name)).c_str(),
                                   hx, (g_dir + L"\\choices.ini").c_str());
    }
    g_last_check = 0; // share now
}

void cycle(Kind k, int dir)
{
    auto opts = options(k);
    if (opts.empty())
        return;
    Option cur = mine(k);
    int i = 0;
    for (int j = 0; j < (int)opts.size(); j++)
        if (opts[j].key == cur.key)
            i = j;
    i = ((i + dir) % (int)opts.size() + (int)opts.size()) % (int)opts.size();
    choose(k, opts[i].key);
}

std::string status()
{
    std::string s;
    char b[200];
    snprintf(b, sizeof b, "creations: in=%u out=%u registered=%u avatar_swaps=%u%s%s\n", g_bundles_in, g_bundles_out,
             g_registered, g_swaps, g_last_error.empty() ? "" : " error=", g_last_error.c_str());
    s += b;
    if (uint8_t* g = modes::cell_game(); g && game::mode_id() == modes::kCell) {
        uint8_t* av = modes::cell_at(*(uint32_t*)(g + 0x411C));
        Key ck = cell_key();
        snprintf(b, sizeof b, "  cell: player model %08X, avatar model %08X\n", ck.instance,
                 av ? *(uint32_t*)(av + 0xFC) : 0u);
        s += b;
    }
    for (int k = 0; k < kKinds; k++) {
        Option o = mine((Kind)k);
        snprintf(b, sizeof b, "  mine %s: %08X '%s' (%u own)\n", kInfo[k].name, o.key.instance, o.name.c_str(),
                 (unsigned)g_opts[k].size());
        s += b;
    }
    for (auto& [ok, ap] : g_applied) {
        snprintf(b, sizeof b, "  player %u %s: %08X/%08X/%08X '%s'\n", ok.first, kInfo[ok.second].name, ap.key.instance,
                 ap.key.type, ap.key.group, ap.name.c_str());
        s += b;
    }
    return s;
}

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && (a[0] == "next" || a[0] == "prev")) {
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        cycle((Kind)k, a[0] == "next" ? 1 : -1);
        return "chosen: " + mine((Kind)k).name;
    }
    if (a.size() >= 2 && a[0] == "choose") { // choose <kind> <hex instance|none>
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        uint32_t inst = a.size() > 2 && a[2] != "none" ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : 0;
        choose((Kind)k, inst ? Key{inst, kInfo[k].type, kInfo[k].group} : Key{});
        return "chosen";
    }
    if (!a.empty() && a[0] == "list") {
        std::string s;
        for (int k = 0; k < kKinds; k++)
            for (auto& o : options((Kind)k))
                s += std::string(kInfo[k].name) + " " + hex(o.key.instance) + " " + o.name + "\n";
        return s;
    }
    return status();
}

} // namespace creations
