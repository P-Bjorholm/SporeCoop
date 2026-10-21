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

static std::deque<Out> g_out;
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

bool share_with(uint8_t peer, std::string& err)
{
    std::wstring slot = newest_slot();
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

void tick(net::Session& s)
{
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
        int budget = 48;
        while (budget-- > 0 && s.pending_to(o.peer) < 96 && !o.done) {
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
static std::wstring pick_slot(uint32_t world_id, const std::string& planet_spo)
{
    std::wstring dir = games_dir();
    CreateDirectoryW(dir.c_str(), nullptr);
    char want[16];
    snprintf(want, sizeof want, "%08X", world_id);
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        std::vector<uint8_t> marker;
        if (read_file(p + L"\\sporecoop.txt", marker)) {
            std::string m(marker.begin(), marker.end());
            if (m.find("host_slot") != std::string::npos)
                return p; // a previous co-op world: update it
        }
    }
    if (!planet_spo.empty()) {
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
    for (auto& [i, f] : g_in.files)
        rels.push_back(f.rel);
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
    g_feed("world of " + host_name + " saved as " + slot_name + ": load it from the main menu");
    g_in = In{};
}

void on_message(uint8_t type, net::Reader& r, const std::string& host_name)
{
    switch (type) {
    case net::MSG_WORLD_OFFER:
        g_in = In{};
        if (g_in.offer.read(r)) {
            g_in.active = true;
            g_feed("receiving world from " + host_name + " (" + std::to_string(g_in.offer.total_size / 1024) +
                   " KB)");
        }
        break;
    case net::MSG_WORLD_FILE: {
        net::WorldFile wf;
        if (g_in.active && wf.read(r)) {
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
            if (it != g_in.files.end() && ch.offset + ch.data.size() <= it->second.data.size()) {
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

} // namespace world
