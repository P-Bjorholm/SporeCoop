// Received worlds on disk: see slotfs.h
#include "slotfs.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace slotfs {

bool g_busy_fix = true, g_match_fix = true, g_write_fix = true;

std::string narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static bool same_dir(std::wstring a, const std::wstring& lower_b)
{
    for (auto& ch : a)
        ch = towlower(ch);
    return a == lower_b;
}

static std::string read_text(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    char b[1024];
    DWORD got = 0;
    if (!ReadFile(f, b, sizeof b, &got, nullptr))
        got = 0;
    CloseHandle(f);
    return std::string(b, got);
}

// "host_slot <id>\nhost <name>\nplanet <name>\n" (world.cpp writes it into every received slot)
static std::string marker_field(const std::string& m, const char* key)
{
    std::string k = std::string(key) + " ";
    for (size_t p = 0; p < m.size();) {
        size_t e = m.find('\n', p);
        if (e == std::string::npos)
            e = m.size();
        std::string line = m.substr(p, e - p);
        while (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.compare(0, k.size(), k) == 0)
            return line.substr(k.size());
        p = e + 1;
    }
    return {};
}

static void rmtree(const std::wstring& d)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..")
                continue;
            std::wstring p = d + L"\\" + n;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(p.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                rmtree(p);
            else
                DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}

static std::wstring stamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t b[64];
    swprintf(b, 64, L"-%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

std::wstring pick(const std::wstring& dir, const std::wstring& busy, const std::string& host,
                  const std::string& planet, const std::string& planet_spo, bool backup_planet, std::string& note)
{
    CreateDirectoryW(dir.c_str(), nullptr);
    note.clear();
    // never the slot the game is playing now (a guest gets the next phase in-game): co-op worlds
    // then alternate between two slots
    auto is_busy = [&](const std::wstring& p) { return !busy.empty() && same_dir(p, busy); };
    std::wstring any_coop; // a co-op slot of another world (last resort when no GameN is free)
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        if (is_busy(p))
            continue;
        std::string m = read_text(p + L"\\sporecoop.txt");
        if (m.find("host_slot") == std::string::npos)
            continue;
        // the same host world: update it. Before (BUG-0087) any co-op slot was taken, also one of
        // another host, and the new files were mixed with the old ones. The world id (a crc of
        // the files) changes with every save, so the host's name and the planet identify it.
        if (!g_match_fix || (marker_field(m, "host") == host && marker_field(m, "planet") == planet))
            return p;
        if (any_coop.empty())
            any_coop = p;
    }
    if (!planet_spo.empty() && backup_planet) { // menu loading needs the disc of this planet free
        for (int k = 0; k < 64; k++) {
            std::wstring p = dir + L"\\Game" + std::to_wstring(k);
            // BUG-0035: not the slot being played (a guest with auto_load_world=0 playing its co-op
            // slot had it moved away and rewritten under the running game)
            if (g_busy_fix && is_busy(p))
                continue;
            if (!exists(p + L"\\" + widen(planet_spo)))
                continue;
            // keep the other copy of this planet outside the Games folder
            bool coop = read_text(p + L"\\sporecoop.txt").find("host_slot") != std::string::npos;
            std::wstring bdir = dir + L"-sporecoop-backup";
            CreateDirectoryW(bdir.c_str(), nullptr);
            std::wstring dst = bdir + L"\\Game" + std::to_wstring(k) + stamp();
            if (!MoveFileW(p.c_str(), dst.c_str()) && g_busy_fix)
                continue; // files in use: leave it, take another slot
            note = std::string(coop ? "an older co-op copy" : "your own save") + " of this planet was backed up to " +
                   narrow(dst);
            return p;
        }
    }
    for (int k = 0; k < 64; k++) {
        std::wstring p = dir + L"\\Game" + std::to_wstring(k);
        if (!exists(p))
            return p;
    }
    return g_match_fix ? any_coop : std::wstring();
}

static bool write_one(const std::wstring& path, const std::vector<uint8_t>& d, std::string& err)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "cannot create " + narrow(path) + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD w = 0;
    bool ok = d.empty() || (WriteFile(f, d.data(), (DWORD)d.size(), &w, nullptr) && w == d.size());
    DWORD e = GetLastError();
    ok = CloseHandle(f) && ok;
    if (!ok)
        err = "cannot write " + narrow(path) + (e == ERROR_DISK_FULL || e == ERROR_HANDLE_DISK_FULL ? " (disk full)"
                                                : " (error " + std::to_string(e) + ")");
    return ok;
}

static std::wstring native(const std::wstring& root, const std::string& rel)
{
    std::wstring p = root + L"\\" + widen(rel);
    for (auto& c : p)
        if (c == L'/')
            c = L'\\';
    return p;
}

static bool write_into(const std::wstring& root, const std::vector<const File*>& files, const std::string& marker,
                       bool check, std::string& err)
{
    if (!CreateDirectoryW(root.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS && check) {
        err = "cannot create the folder " + narrow(root);
        return false;
    }
    for (const File* f : files) {
        std::wstring p = native(root, f->rel);
        for (size_t q = root.size() + 1; q < p.size(); q++) // sub folders
            if (p[q] == L'\\' && !CreateDirectoryW(p.substr(0, q).c_str(), nullptr) &&
                GetLastError() != ERROR_ALREADY_EXISTS && check) {
                err = "cannot create the folder " + narrow(p.substr(0, q));
                return false;
            }
        if (!write_one(p, f->data, err) && check)
            return false;
    }
    std::vector<uint8_t> m(marker.begin(), marker.end());
    return write_one(root + L"\\sporecoop.txt", m, err) || !check;
}

bool write(const std::wstring& slot, const std::vector<const File*>& files, const std::string& host,
           const std::string& planet, uint32_t world_id, std::string& err)
{
    char head[32];
    snprintf(head, sizeof head, "host_slot %08X\n", world_id);
    std::string marker = head + ("host " + host + "\nplanet " + planet + "\n");
    if (!g_write_fix) { // the old behaviour: in place, every failure skipped
        write_into(slot, files, marker, false, err);
        err.clear();
        return true;
    }
    size_t cut = slot.find_last_of(L'\\');
    std::wstring parent = slot.substr(0, cut), name = slot.substr(cut + 1);
    std::wstring tmproot = parent + L"-sporecoop-tmp"; // next to Games: the galaxy menu never sees it
    CreateDirectoryW(tmproot.c_str(), nullptr);
    std::wstring tag = stamp() + L"-" + std::to_wstring(GetTickCount64() % 100000);
    std::wstring tmp = tmproot + L"\\" + name + tag, aside = tmproot + L"\\" + name + L"-old" + tag;
    rmtree(tmp);
    if (!write_into(tmp, files, marker, true, err)) {
        rmtree(tmp);
        return false;
    }
    bool had = exists(slot);
    if (had && !MoveFileW(slot.c_str(), aside.c_str())) {
        err = "cannot replace " + narrow(slot) + " (a file in it is in use, error " + std::to_string(GetLastError()) +
              ")";
        rmtree(tmp);
        return false;
    }
    if (!MoveFileW(tmp.c_str(), slot.c_str())) {
        err = "cannot move the world into " + narrow(slot) + " (error " + std::to_string(GetLastError()) + ")";
        if (had)
            MoveFileW(aside.c_str(), slot.c_str());
        rmtree(tmp);
        return false;
    }
    if (had)
        rmtree(aside);
    RemoveDirectoryW(tmproot.c_str()); // only when empty
    return true;
}

} // namespace slotfs
