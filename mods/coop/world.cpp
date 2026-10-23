// World sharing: the host streams its current save folder (AppData\Spore\Games\GameN)
// to a joining guest, who stores it as a new save slot. See docs/03-gameplay.md 3.2.
#include "world.h"

#include <winsock2.h>
#define WIN32_LEAN_AND_MEAN
#include <shlobj.h>
#include <windows.h>

#include <cstdio>
#include <deque>
#include <map>

#include "game.h"
#include "sporekit_game.hpp"
#include "modes.h"
#include "sui.h"

namespace world {

static const SkHost* H;
static SkMod* M;
static std::function<void(const std::string&)> g_feed;

struct File {
    std::string rel; // '/' separated
    std::vector<uint8_t> data;
};

struct Out {
    uint8_t peer;
    std::vector<File> files;
    uint32_t world_id = 0;
    std::string planet;
    size_t fi = 0, off = 0;
    bool header_sent = false;
    bool done = false;
};

struct In {
    bool active = false;
    net::WorldOffer offer;
    std::map<uint16_t, File> files;
    uint32_t received = 0;
};

// A received world comes from another player's machine: only plain relative names inside the
// save slot, sane sizes (a save slot is a few MB; a Space save with many systems stays far below)
constexpr uint32_t kMaxWorldBytes = 128u << 20, kMaxWorldFiles = 2048;

static bool safe_rel(const std::string& rel)
{
    if (rel.empty() || rel.size() > 200)
        return false;
    size_t start = 0;
    int depth = 0;
    while (start <= rel.size()) {
        size_t end = rel.find_first_of("/\\", start);
        if (end == std::string::npos)
            end = rel.size();
        std::string part = rel.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || ++depth > 4)
            return false; // also rejects leading or doubled separators
        for (unsigned char c : part)
            if (!(isalnum(c) || c == ' ' || c == '_' || c == '-' || c == '.' || c == '(' || c == ')' || c >= 0x80))
                return false; // no ':' (drives, streams), no control characters
        start = end + 1;
    }
    return true;
}

static std::deque<Out> g_out;
static bool g_auto_load = true;
static In g_in;

static std::wstring games_dir()
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return {};
    return std::wstring(p) + L"\\Spore\\Games";
}

static std::string narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
static std::wstring widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static bool read_file(const std::wstring& p, std::vector<uint8_t>& out)
{
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f)
        return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static void collect(const std::wstring& root, const std::wstring& rel, std::vector<File>& out)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((root + L"\\" + rel + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..")
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            collect(root, rel + n + L"\\", out);
        } else {
            File f;
            std::wstring r = rel + n;
            if (r == L"sporecoop.txt")
                continue;
            if (!read_file(root + L"\\" + r, f.data))
                continue;
            std::string s = narrow(r);
            for (auto& c : s)
                if (c == '\\')
                    c = '/';
            f.rel = s;
            out.push_back(std::move(f));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// newest Games\Game* folder = the slot the host played/saved last
static std::wstring newest_slot()
{
    std::wstring dir = games_dir(), best;
    FILETIME bt{};
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\Game*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        // the folder time does not change when files inside are rewritten: use lastSave.pld
        WIN32_FILE_ATTRIBUTE_DATA a;
        std::wstring p = dir + L"\\" + fd.cFileName;
        if (!GetFileAttributesExW((p + L"\\lastSave.pld").c_str(), GetFileExInfoStandard, &a))
            continue;
        if (best.empty() || CompareFileTime(&a.ftLastWriteTime, &bt) > 0) {
            best = p;
            bt = a.ftLastWriteTime;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

void init(const SkHost* host, SkMod* mod, std::function<void(const std::string&)> feed)
{
    H = host;
    M = mod;
    g_feed = std::move(feed);
}

// slot holding the planet file the game is playing now (newest copy), else the newest slot
static std::wstring current_slot(std::string& planet)
{
    planet = modes::current_world_name(); // "Name.spo"
    std::wstring dir = games_dir(), best;
    if (!planet.empty()) {
        FILETIME bt{};
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\Game*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                std::wstring p = dir + L"\\" + fd.cFileName;
                WIN32_FILE_ATTRIBUTE_DATA a;
                if (!GetFileAttributesExW((p + L"\\" + widen(planet)).c_str(), GetFileExInfoStandard, &a))
                    continue;
                if (best.empty() || CompareFileTime(&a.ftLastWriteTime, &bt) > 0) {
                    best = p;
                    bt = a.ftLastWriteTime;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    if (best.empty()) {
        planet.clear();
        best = newest_slot();
    }
    return best;
}

// The game confirms every save with a native box "The game on planet <name> is saved." (UTFWin
// window, control id 0x06318E78, OK button; closed boxes stay in the tree without the visible
// flag). While it is up the simulation is paused (a cell does not move), and sometimes the box
// also holds the frames. It used to be confirmed with one Enter posted 2.5 s after our save: under
// CPU load the box came later, stayed open and the host's game stood still - its frames too, so
// the guest got no host state (no partner cell). Now the box is watched for after each save and
// confirmed once it is visible (UI reads outside the frame); the world is shared when it was seen.
constexpr uint32_t kSavedBoxId = 0x06318E78;
static uint64_t g_saved_at, g_box_watch_until, g_box_seen_at, g_box_next_check, g_box_last_enter;
static bool g_box_task;

static bool any_visible(sui::Win w, uint32_t id, int depth = 0)
{
    if (!w || depth > 12 || !sui::visible(w))
        return false;
    if (sui::control_id(w) == id)
        return true;
    for (sui::Win c : sui::children(w))
        if (any_visible(c, id, depth + 1))
            return true;
    return false;
}

static void box_task(void*)
{
    g_box_task = false;
    if (!any_visible(sui::main_window(), kSavedBoxId))
        return;
    uint64_t now = GetTickCount64();
    if (!g_box_seen_at)
        H->log(M, SK_INFO, "world: the game saved box is up %llu ms after the save: confirming it",
               (unsigned long long)(now - g_saved_at));
    g_box_seen_at = now;
    if (now - g_box_last_enter < 400)
        return;
    g_box_last_enter = now;
    if (HWND w = (HWND)H->game_window()) {
        PostMessageW(w, WM_KEYDOWN, VK_RETURN, 0x001C0001);
        PostMessageW(w, WM_KEYUP, VK_RETURN, 0xC01C0001);
    }
}

static void box_tick()
{
    uint64_t now = GetTickCount64();
    if (now >= g_box_watch_until || g_box_task || now < g_box_next_check)
        return;
    g_box_next_check = now + 200;
    g_box_task = true;
    H->run_outside_frame(box_task, nullptr);
}

bool save_game(std::string& err)
{
    using namespace skg;
    uintptr_t get = H->sym("App::IMessageManager::Get");
    void* mm = get ? cdecl_call<void*>(get) : nullptr;
    if (!mm) {
        err = "no message manager";
        return false;
    }
    // space: the game saves only outside planets (a save made on a planet loads into a broken
    // state - verified: the space UI faults every frame), so wait until the player leaves it
    if (game::mode_id() == 0x1654C05) {
        uintptr_t ctx = H->sym("Simulator::GetCurrentContext");
        if (ctx && skg::cdecl_call<int>(ctx) == 0) {
            err = "leave the planet to save (space stage saves only in a solar system or the galaxy)";
            return false;
        }
    }
    // MessagePost like the pause menu button (4 arguments, as pushed at 0xE03149): the save then runs
    // in the game's own message processing. A synchronous MessageSend from our frame hook (inside
    // Present) deadlocked DXVK threads.
    vcall<void>(mm, 0x18 / 4, 0x1CD20F0u, (void*)nullptr, (void*)nullptr, 0);
    g_saved_at = GetTickCount64();
    g_box_seen_at = 0;
    g_box_watch_until = g_saved_at + 30000;
    return true;
}

struct PendingShare {
    uint8_t peer;
    uint64_t at;   // share from this tick count (files are written asynchronously after a save)
    bool need_save;
    uint64_t latest = 0; // ... once the saved box was seen, or at this tick count without it
    uint64_t next_try = 0;
    bool told = false;
};
static std::vector<PendingShare> g_pending;

bool share_with(uint8_t peer, std::string& err, bool save_first)
{
    if (save_first) {
        g_pending.push_back({peer, 0, true});
        return true;
    }
    return share_now(peer, err);
}

bool share_now(uint8_t peer, std::string& err)
{
    std::string cur_planet;
    std::wstring slot = current_slot(cur_planet);
    if (slot.empty()) {
        err = "no saved game found (save the game before hosting)";
        return false;
    }
    Out o;
    o.peer = peer;
    collect(slot, L"", o.files);
    if (o.files.empty()) {
        err = "save folder is empty";
        return false;
    }
    uint32_t crc = 0, total = 0;
    for (auto& f : o.files) {
        crc = net::crc32_update(crc, (const uint8_t*)f.rel.data(), f.rel.size());
        crc = net::crc32_update(crc, f.data.data(), f.data.size());
        total += (uint32_t)f.data.size();
    }
    o.world_id = crc;
    o.planet = narrow(slot.substr(slot.find_last_of(L'\\') + 1));
    for (auto& f : o.files) // planet name = <name>.spo in the slot root
        if (f.rel.size() > 4 && f.rel.compare(f.rel.size() - 4, 4, ".spo") == 0 && f.rel.find('/') == std::string::npos)
            o.planet = f.rel.substr(0, f.rel.size() - 4);
    if (cur_planet.size() > 4) // a slot can hold several planets: prefer the one being played
        o.planet = cur_planet.substr(0, cur_planet.size() - 4);
    net::WorldOffer off;
    off.world_id = crc;
    off.total_size = total;
    off.file_count = (uint16_t)o.files.size();
    off.planet = o.planet;
    H->log(M, SK_INFO, "world: sharing %s (%u files, %u bytes, id %08X) with player %u",
           narrow(slot).c_str(), (unsigned)o.files.size(), total, crc, peer);
    o.header_sent = false;
    g_out.push_back(std::move(o));
    // offer is sent by tick() so it is ordered before the files
    (void)off;
    return true;
}

// A load requested while a stage runs goes through the galaxy menu (see load_slot)
struct ViaMenu {
    bool on = false;
    std::wstring slot;
    std::string planet;
    double since = 0, menu_since = 0;
};
static ViaMenu g_via_menu;
static const double kMenuSettle = 3.0;
static bool load_now(const std::wstring& slot, const std::string& planet_file, std::string& err);

static void via_menu_tick()
{
    if (!g_via_menu.on)
        return;
    double now = H->time_seconds();
    if (game::mode_id() == modes::kMenu) {
        // the galaxy menu reads the stars of its slot and shows its planet when it opens: let it
        // settle first (switching the slot under it right away faulted in its planet view)
        if (!g_via_menu.menu_since) {
            g_via_menu.menu_since = now;
            H->log(M, SK_INFO, "world: in the galaxy menu %.1f s after leaving the stage", now - g_via_menu.since);
        }
        if (now - g_via_menu.menu_since < kMenuSettle)
            return;
        // switching the save folder and starting the load reads files and builds UI: outside the
        // frame (this tick also runs inside Present)
        static ViaMenu v;
        v = g_via_menu;
        g_via_menu = ViaMenu{};
        H->run_outside_frame(
            [](void*) {
                std::string err;
                if (!load_now(v.slot, v.planet, err))
                    g_feed("the world could not be loaded: " + err);
            },
            nullptr);
        return;
    }
    g_via_menu.menu_since = 0;
    if (now - g_via_menu.since > 30.0) {
        g_via_menu = ViaMenu{};
        H->log(M, SK_WARN, "world: the game did not go to the galaxy menu, load cancelled");
        g_feed("the world could not be loaded: the game did not go to the galaxy menu");
    }
}

struct Deferred {
    bool on = false;
    std::wstring slot;
    std::string planet, host;
    double back_since = 0;
};
static Deferred g_deferred;

// a received world waits while the player is in an editor; loaded 2 s after it is back in a stage
static void deferred_tick()
{
    if (!g_deferred.on)
        return;
    uint32_t mode = game::mode_id();
    double now = H->time_seconds();
    if (mode == modes::kEditor || mode == modes::kEdit || mode == modes::kLoad) {
        g_deferred.back_since = 0;
        return;
    }
    if (!g_deferred.back_since) {
        g_deferred.back_since = now;
        return;
    }
    if (now - g_deferred.back_since < 2.0)
        return;
    Deferred d = g_deferred;
    g_deferred = Deferred{};
    std::string err;
    if (load_slot(d.slot, d.planet, err))
        g_feed("loading the world of " + d.host + "...");
    else
        g_feed("the world of " + d.host + " could not be loaded: " + err);
}

void guest_tick()
{
    via_menu_tick();
    deferred_tick();
    box_tick();
}

void tick(net::Session& s)
{
    via_menu_tick();
    deferred_tick();
    box_tick();
    for (size_t i = 0; i < g_pending.size();) {
        auto& p = g_pending[i];
        uint64_t now = GetTickCount64();
        if (p.need_save) {
            if (now >= p.next_try) {
                std::string why;
                if (save_game(why)) {
                    p.need_save = false;
                    p.at = now + 2500;
                    p.latest = now + 10000;
                } else {
                    p.next_try = now + 2000;
                    if (!p.told)
                        g_feed("a player is waiting for the world: " + why);
                    p.told = true;
                }
            }
            i++;
            continue;
        }
        // the save is complete when its box shows up (box_tick confirms it)
        if (now < p.at || (p.latest && now < p.latest && g_box_seen_at < g_saved_at)) {
            i++;
            continue;
        }
        if (p.latest && g_box_seen_at < g_saved_at)
            H->log(M, SK_INFO, "world: no saved box %llu ms after the save: sharing anyway",
                   (unsigned long long)(now - g_saved_at));
        std::string err;
        if (!share_now(g_pending[i].peer, err))
            g_feed("cannot share the world: " + err);
        g_pending.erase(g_pending.begin() + i); // p is invalid from here
    }
    for (auto& o : g_out) {
        if (o.done)
            continue;
        if (!o.header_sent) {
            net::WorldOffer off;
            off.world_id = o.world_id;
            off.file_count = (uint16_t)o.files.size();
            for (auto& f : o.files)
                off.total_size += (uint32_t)f.data.size();
            off.planet = o.planet;
            net::Writer w;
            off.write(w);
            if (!s.send_reliable_to(o.peer, net::MSG_WORLD_OFFER, w)) {
                o.done = true; // peer gone
                continue;
            }
            o.header_sent = true;
        }
        // flow control: keep a bounded number of reliable messages in flight
        // a LAN / local peer (short round trip) gets a much bigger window
        int rtt = s.rtt_of(o.peer);
        size_t window = rtt >= 0 && rtt < 40 ? 600 : 160;
        int budget = 300;
        while (budget-- > 0 && s.pending_to(o.peer) < window && !o.done) {
            if (o.fi >= o.files.size()) {
                net::Writer w;
                w.u32(o.world_id);
                s.send_reliable_to(o.peer, net::MSG_WORLD_DONE, w);
                o.done = true;
                H->log(M, SK_INFO, "world: transfer to %u queued completely", o.peer);
                break;
            }
            File& f = o.files[o.fi];
            if (o.off == 0) {
                net::WorldFile wf;
                wf.index = (uint16_t)o.fi;
                wf.size = (uint32_t)f.data.size();
                wf.path = f.rel;
                net::Writer w;
                wf.write(w);
                s.send_reliable_to(o.peer, net::MSG_WORLD_FILE, w);
            }
            net::WorldChunk ch;
            ch.index = (uint16_t)o.fi;
            ch.offset = (uint32_t)o.off;
            size_t n = std::min<size_t>(1000, f.data.size() - o.off);
            ch.data.assign(f.data.begin() + o.off, f.data.begin() + o.off + n);
            net::Writer w;
            ch.write(w);
            s.send_reliable_to(o.peer, net::MSG_WORLD_CHUNK, w);
            o.off += n;
            if (o.off >= f.data.size()) {
                o.fi++;
                o.off = 0;
            }
        }
    }
    while (!g_out.empty() && g_out.front().done)
        g_out.pop_front();
}

static std::string planet_file(const std::vector<std::string>& rels)
{
    for (auto& r : rels)
        if (r.size() > 4 && r.compare(r.size() - 4, 4, ".spo") == 0 && r.find('/') == std::string::npos)
            return r;
    return {};
}

static bool dir_has(const std::wstring& dir, const std::wstring& file)
{
    return GetFileAttributesW((dir + L"\\" + file).c_str()) != INVALID_FILE_ATTRIBUTES;
}

// Spore shows a save as a disc at its star in the galaxy menu, so two slots of the
// same planet overlap. Reuse, in order: the slot that already holds this host world,
// a slot of the same planet (backed up first), else the first free GameN.
// folder the save database points to now (the slot being played), lower case, no trailing '\'
static std::wstring current_db_dir()
{
    uintptr_t getdb = H->sym("Resource::GetDatabaseByID");
    void* db = getdb ? skg::cdecl_call<void*>(getdb, 0x4729A47u) : nullptr;
    const char16_t* loc = db ? skg::vcall<const char16_t*>(db, 0x28 / 4) : nullptr;
    std::wstring w;
    for (const char16_t* c = loc; c && *c && w.size() < 520; c++)
        w += (wchar_t)(*c == u'/' ? u'\\' : *c);
    while (!w.empty() && w.back() == L'\\')
        w.pop_back();
    for (auto& ch : w)
        ch = towlower(ch);
    return w;
}

static bool same_dir(std::wstring a, const std::wstring& lower_b)
{
    for (auto& ch : a)
        ch = towlower(ch);
    return a == lower_b;
}

static std::wstring pick_slot(uint32_t world_id, const std::string& planet_spo)
{
    std::wstring dir = games_dir();
    CreateDirectoryW(dir.c_str(), nullptr);
    char want[16];
    snprintf(want, sizeof want, "%08X", world_id);
    // never overwrite the slot the game is playing now (a guest gets the next phase in-game):
    // co-op worlds then alternate between two slots
    std::wstring busy = current_db_dir();
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        std::vector<uint8_t> marker;
        if (!busy.empty() && same_dir(p, busy))
            continue;
        if (read_file(p + L"\\sporecoop.txt", marker)) {
            std::string m(marker.begin(), marker.end());
            if (m.find("host_slot") != std::string::npos)
                return p; // a previous co-op world: update it
        }
    }
    if (!planet_spo.empty() && !g_auto_load) { // menu loading needs the disc of this planet free
        for (int k = 0; k < 64; k++) {
            std::wstring p = dir + L"\\Game" + std::to_wstring(k);
            if (dir_has(p, widen(planet_spo))) {
                // keep the player's own copy of this planet outside the Games folder
                SYSTEMTIME st;
                GetLocalTime(&st);
                wchar_t b[64];
                swprintf(b, 64, L"-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
                         st.wMinute, st.wSecond);
                std::wstring bdir = dir + L"-sporecoop-backup";
                CreateDirectoryW(bdir.c_str(), nullptr);
                std::wstring dst = bdir + L"\\Game" + std::to_wstring(k) + b;
                MoveFileW(p.c_str(), dst.c_str());
                g_feed("your own save of this planet was backed up to " + narrow(dst));
                return p;
            }
        }
    }
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES)
            return p;
    }
    return {};
}


// Point the save-folder database (resource DB 0x4729A47) to `slot` and load `planet_file`
// the way the galaxy menu's Play button does (see docs/05-re-notes.md).
void set_auto_load(bool on) { g_auto_load = on; }

static bool in_stage(uint32_t m)
{
    return m == modes::kCell || m == modes::kCreature || m == modes::kTribe || m == modes::kCiv ||
           m == modes::kSpace;
}

// A save is loaded from the galaxy menu, the way the game itself does it, never straight from a
// running stage (docs/re/space.md "Loading a save while a stage runs"): from Space that crashed
// (also the game's own ReloadCurrentGame: kLoadGameMode is entered with the active star already
// cleared and the space mode's exit 0xFDCED0 -> 0xFDA9E0 leaves the solar system of a null cStar),
// from Cell the cell pool was freed under our adapter, and a space save loaded from Cell kept the
// old CurrentGameName ("Karke.spo"), so the next save wrote the wrong planet. The game's quit
// message 0x4715068 (handled by 0xB63610) saves the game and leaves any stage for the galaxy menu
// (in space: the planet first); the save is loaded once the menu is up (~1 s + kMenuSettle).
bool load_slot(const std::wstring& slot, const std::string& planet_file, std::string& err)
{
    if (g_via_menu.on || in_stage(game::mode_id())) {
        bool posted = g_via_menu.on;
        if (!posted) {
            uintptr_t get = H->sym("App::IMessageManager::Get");
            void* mm = get ? skg::cdecl_call<void*>(get) : nullptr;
            if (!mm) {
                err = "no message manager";
                return false;
            }
            // MessagePost (4 arguments, as save_game): handled in the game's own message
            // processing, outside our frame hook
            skg::vcall<void>(mm, 0x18 / 4, 0x4715068u, (void*)nullptr, (void*)nullptr, 0);
            H->log(M, SK_INFO, "world: leaving the stage to the galaxy menu before loading %s",
                   planet_file.c_str());
        }
        double since = posted ? g_via_menu.since : H->time_seconds();
        g_via_menu = ViaMenu{true, slot, planet_file, since, 0}; // the newest request wins
        return true;
    }
    return load_now(slot, planet_file, err);
}

static bool load_now(const std::wstring& slot, const std::string& planet_file, std::string& err)
{
    uintptr_t getdb = H->sym("Resource::GetDatabaseByID");
    uintptr_t getmgr = H->sym("Simulator::cGamePersistenceManager::Get");
    uintptr_t loadby = H->sym("Simulator::cGamePersistenceManager::LoadGameByName");
    if (!getdb || !getmgr || !loadby) {
        err = "load symbols missing";
        return false;
    }
    void* db = skg::cdecl_call<void*>(getdb, 0x4729A47u);
    void* mgr = skg::cdecl_call<void*>(getmgr);
    if (!db || !mgr) {
        err = "save database not available";
        return false;
    }
    std::u16string dir;
    for (wchar_t c : slot)
        dir += (char16_t)(c == L'\\' ? L'/' : c);
    if (dir.empty() || dir.back() != u'/')
        dir += u'/';
    std::u16string name;
    for (char c : planet_file)
        name += (char16_t)(unsigned char)c;
    skg::vcall<bool>(db, 0x1C / 4); // Close
    if (!skg::vcall<bool>(db, 0x2C / 4, dir.c_str())) { // SetLocation
        err = "SetLocation failed";
        return false;
    }
    if (!skg::vcall<bool>(db, 0x18 / 4, 3, 6, 0)) { // Open(ReadWrite, CD::Default, autoOpen=false)
        err = "Open failed";
        return false;
    }
    // the star manager keeps the stars of the previously selected slot: reload them
    uintptr_t starget = H->sym("Simulator::cStarManager::Get"), readstars = H->sym("Simulator::cStarManager::ReadStarsFromSaveDB");
    if (starget && readstars)
        if (void* sm = skg::cdecl_call<void*>(starget))
            skg::thiscall<void>(readstars, sm);
    skg::thiscall<void>(loadby, mgr, name.c_str());
    H->log(M, SK_INFO, "world: loading %s from %s", planet_file.c_str(), narrow(slot).c_str());
    return true;
}

static void finish(const std::string& host_name)
{
    uint32_t crc = 0;
    for (auto& [i, f] : g_in.files) {
        crc = net::crc32_update(crc, (const uint8_t*)f.rel.data(), f.rel.size());
        crc = net::crc32_update(crc, f.data.data(), f.data.size());
    }
    if (crc != g_in.offer.world_id) {
        g_feed("world transfer failed: checksum mismatch");
        g_in = In{};
        return;
    }
    std::vector<std::string> rels;
    for (auto& [i, f] : g_in.files) {
        if (!safe_rel(f.rel)) { // checked on arrival already; never write outside the slot
            g_in = In{};
            return;
        }
        rels.push_back(f.rel);
    }
    std::wstring slot = pick_slot(crc, planet_file(rels));
    if (slot.empty()) {
        g_feed("world transfer failed: no free save slot");
        g_in = In{};
        return;
    }
    CreateDirectoryW(slot.c_str(), nullptr);
    for (auto& [i, f] : g_in.files) {
        std::wstring p = slot + L"\\" + widen(f.rel);
        for (auto& c : p)
            if (c == L'/')
                c = L'\\';
        // create sub folders
        for (size_t q = slot.size() + 1; q < p.size(); q++)
            if (p[q] == L'\\')
                CreateDirectoryW(p.substr(0, q).c_str(), nullptr);
        FILE* fp = _wfopen(p.c_str(), L"wb");
        if (fp) {
            fwrite(f.data.data(), 1, f.data.size(), fp);
            fclose(fp);
        }
    }
    FILE* fp = _wfopen((slot + L"\\sporecoop.txt").c_str(), L"w");
    if (fp) {
        fprintf(fp, "host_slot %08X\nhost %s\nplanet %s\n", crc, host_name.c_str(), g_in.offer.planet.c_str());
        fclose(fp);
    }
    std::string slot_name = narrow(slot.substr(slot.find_last_of(L'\\') + 1));
    H->log(M, SK_INFO, "world: received %u bytes into %s", g_in.received, narrow(slot).c_str());
    std::string planet;
    for (auto& r : rels)
        if (r.size() > 4 && r.compare(r.size() - 4, 4, ".spo") == 0 && r.find('/') == std::string::npos)
            planet = r;
    // a slot can hold several planets: the host names the one it plays
    for (auto& r : rels)
        if (!g_in.offer.planet.empty() && r == g_in.offer.planet + ".spo")
            planet = r;
    g_in = In{};
    std::string err;
    // "snapshot.spo" is the game's temporary file of an unsaved game: loading it crashes
    if (planet == "snapshot.spo") {
        planet.clear();
        err = "the host has not saved its game yet";
    }
    // a player in its own editor (everybody evolves its own creature) loads it when it is back:
    // loading a world under an open editor crashed the game
    uint32_t mode = game::mode_id();
    if (g_auto_load && !planet.empty() && (mode == modes::kEditor || mode == modes::kEdit)) {
        g_deferred = Deferred{true, slot, planet, host_name, 0};
        g_feed("the world of " + host_name + " is loaded when you leave the editor");
        return;
    }
    if (g_auto_load && !planet.empty() && load_slot(slot, planet, err)) {
        g_feed("loading the world of " + host_name + "...");
        return;
    }
    g_feed("world of " + host_name + " saved as " + slot_name + ": load it from the main menu" +
           (err.empty() ? "" : " (" + err + ")"));
}

void on_message(uint8_t type, net::Reader& r, const std::string& host_name)
{
    switch (type) {
    case net::MSG_WORLD_OFFER:
        g_in = In{};
        if (g_in.offer.read(r) && g_in.offer.total_size <= kMaxWorldBytes && g_in.offer.file_count <= kMaxWorldFiles) {
            g_in.active = true;
            g_feed("receiving world from " + host_name + " (" + std::to_string(g_in.offer.total_size / 1024) +
                   " KB)");
        }
        break;
    case net::MSG_WORLD_FILE: {
        net::WorldFile wf;
        if (g_in.active && wf.read(r)) {
            uint64_t sum = wf.size;
            for (auto& [i, f] : g_in.files)
                if (i != wf.index)
                    sum += f.data.size();
            if (!safe_rel(wf.path) || wf.index >= g_in.offer.file_count || sum > g_in.offer.total_size) {
                H->log(M, SK_WARN, "world: rejected file %u '%s' (%u bytes) from %s", wf.index, wf.path.c_str(),
                       wf.size, host_name.c_str());
                g_feed("the world from " + host_name + " was rejected (bad file name or size)");
                g_in = In{};
                break;
            }
            File& f = g_in.files[wf.index];
            f.rel = wf.path;
            f.data.resize(wf.size);
        }
        break;
    }
    case net::MSG_WORLD_CHUNK: {
        net::WorldChunk ch;
        if (g_in.active && ch.read(r)) {
            auto it = g_in.files.find(ch.index);
            if (it != g_in.files.end() && (uint64_t)ch.offset + ch.data.size() <= it->second.data.size()) {
                memcpy(it->second.data.data() + ch.offset, ch.data.data(), ch.data.size());
                g_in.received += (uint32_t)ch.data.size();
            }
        }
        break;
    }
    case net::MSG_WORLD_DONE:
        if (g_in.active)
            finish(host_name);
        break;
    }
}

bool progress(uint32_t& got, uint32_t& total)
{
    got = g_in.received;
    total = g_in.offer.total_size;
    return g_in.active;
}

std::string status()
{
    std::string s;
    char b[200];
    for (auto& o : g_out) {
        snprintf(b, sizeof b, "sending world to %u: file %u/%u\n", o.peer, (unsigned)o.fi, (unsigned)o.files.size());
        s += b;
    }
    if (g_in.active) {
        snprintf(b, sizeof b, "receiving world: %u/%u bytes\n", g_in.received, g_in.offer.total_size);
        s += b;
    }
    return s;
}

bool load_local(const std::string& slot_name, const std::string& planet, std::string& err)
{
    std::string file = planet.size() > 4 && planet.compare(planet.size() - 4, 4, ".spo") == 0 ? planet : planet + ".spo";
    std::wstring slot = games_dir() + L"\\" + widen(slot_name);
    if (GetFileAttributesW((slot + L"\\" + widen(file)).c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "no " + file + " in " + narrow(slot);
        return false;
    }
    return load_slot(slot, file, err);
}

} // namespace world
