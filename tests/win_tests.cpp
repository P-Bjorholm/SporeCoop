// Windows-side unit tests: the file handling of the updater and of received worlds against the
// real Win32 file API (Wine in a lane's bottle: tools/tests/win_unit.py; or Windows). Each test
// works in its own folder under %TEMP%. `win_tests.exe --old` switches the fixes off: every
// check of a fixed bug must then fail.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../mods/coop/slotfs.h"
#include "../sporekit/inifile.h"
#include "../updater/winupdate.h"

static int g_fail;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static std::wstring g_tmp;

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void put(const std::wstring& p, const std::string& text)
{
    for (size_t q = g_tmp.size() + 1; q < p.size(); q++)
        if (p[q] == L'\\')
            CreateDirectoryW(p.substr(0, q).c_str(), nullptr);
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w;
    WriteFile(f, text.data(), (DWORD)text.size(), &w, nullptr);
    CloseHandle(f);
}

static std::string get(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return "<missing>";
    char b[4096];
    DWORD got = 0;
    ReadFile(f, b, sizeof b, &got, nullptr);
    CloseHandle(f);
    return std::string(b, got);
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
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                rmtree(p);
            else
                DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}

static std::wstring fresh_dir(const wchar_t* name)
{
    std::wstring d = g_tmp + L"\\" + name;
    rmtree(d);
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// BUG-0114: only the updater's own backups are removed, never the player's *.old files
static void test_cleanup_old()
{
    printf("updater: cleanup_old keeps the player's *.old files\n");
    std::wstring g = fresh_dir(L"cleanup");
    put(g + L"\\SporebinEP1\\SporeApp.exe.old", "player's backup");
    put(g + L"\\Data\\keep.old", "another mod");
    put(g + L"\\SporebinEP1\\mods\\othermod.ini.old", "another mod");
    put(g + L"\\SporebinEP1\\steam_api.dll.old", "the player's own steam_api backup (not our loader)");
    put(g + L"\\SporebinEP1\\sporekit.dll.sporecoop-old", "ours");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old", "ours");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.old", "ours, from a version before the fix");
    put(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym.old", "ours, old version");
    put(g + L"\\Data\\SporeCoop.package.sporecoop-old", "ours");
    upd::cleanup_old(g);
    CHECK(exists(g + L"\\SporebinEP1\\SporeApp.exe.old"));
    CHECK(exists(g + L"\\Data\\keep.old"));
    CHECK(exists(g + L"\\SporebinEP1\\mods\\othermod.ini.old"));
    CHECK(exists(g + L"\\SporebinEP1\\steam_api.dll.old"));
    CHECK(!exists(g + L"\\SporebinEP1\\sporekit.dll.sporecoop-old") || !upd::g_old_fix);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old") || !upd::g_old_fix);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\coop.dll.old"));
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym.old"));
    CHECK(!exists(g + L"\\Data\\SporeCoop.package.sporecoop-old") || !upd::g_old_fix);
}

// BUG-0089 (in apply_pack too) and the backup name: a pack replaces a file, the backup is ours
static void test_apply_pack()
{
    printf("updater: apply_pack backups and a file listed twice\n");
    std::wstring g = fresh_dir(L"apply");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "ORIGINAL");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.old", "player's backup");
    upd::Pack p;
    p.version = "9";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '1'}});
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '2'}});
    std::string err;
    bool ok = upd::apply_pack(g, p, err);
    CHECK(!ok); // a file listed twice: nothing installed
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "ORIGINAL");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.old") == "player's backup");
    p.files.pop_back();
    CHECK(upd::apply_pack(g, p, err));
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "N1");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.old") == "player's backup");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old") == "ORIGINAL");
}

// BUG-0150: a deferred file whose swap fails at the next start stays listed and is applied later
static void test_pending()
{
    printf("updater: apply_pending keeps an entry whose swap failed\n");
    std::wstring g = fresh_dir(L"pending");
    std::wstring pkg = g + L"\\Data\\SporeCoop.package", list = g + L"\\SporebinEP1\\mods\\sporecoop.pending";
    put(pkg, "OLD");
    put(pkg + L".new", "NEW");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.new", "NEWDLL");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "OLDDLL");
    put(list, "Data/SporeCoop.package\nSporebinEP1/mods/coop.dll\n");
    // another instance of the game holds the package (no sharing at all)
    HANDLE hold = CreateFileW(pkg.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    upd::apply_pending(g);
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "NEWDLL");
    CloseHandle(hold);
    CHECK(exists(list));
    CHECK(get(list) == "Data/SporeCoop.package\n");
    upd::apply_pending(g); // the start after that
    CHECK(get(pkg) == "NEW");
    CHECK(!exists(pkg + L".new"));
    CHECK(!exists(list));
}

// BUG-0035, BUG-0087, BUG-0105: where a received world goes and how it is written
static std::vector<slotfs::File> world(const char* planet, const char* data)
{
    std::vector<slotfs::File> v;
    v.push_back({std::string(planet) + ".spo", std::vector<uint8_t>(data, data + strlen(data))});
    v.push_back({"stars.db", std::vector<uint8_t>(data, data + strlen(data))});
    v.push_back({"sub/x.pkp", std::vector<uint8_t>(data, data + strlen(data))});
    return v;
}

static bool wr(const std::wstring& slot, const std::vector<slotfs::File>& w, const char* host, const char* planet,
               uint32_t id, std::string& err)
{
    std::vector<const slotfs::File*> v;
    for (auto& f : w)
        v.push_back(&f);
    return slotfs::write(slot, v, host, planet, id, err);
}

static std::wstring lower(std::wstring s)
{
    for (auto& c : s)
        c = towlower(c);
    return s;
}

static void test_slot_busy()
{
    printf("world: the slot the game plays is never moved or written (BUG-0035)\n");
    std::wstring games = fresh_dir(L"busy") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    auto w = world("Terra", "A1");
    std::wstring s0 = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(s0, w, "Alice", "Terra", 0x11, err));
    // the guest loads that slot by hand (auto_load_world=0) and plays it; the host shares again
    put(s0 + L"\\guest-progress.txt", "the guest's game");
    auto w2 = world("Terra", "A2");
    std::wstring s1 = slotfs::pick(games, lower(s0), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(!s1.empty());
    CHECK(lower(s1) != lower(s0));
    if (!s1.empty() && lower(s1) != lower(s0))
        CHECK(wr(s1, w2, "Alice", "Terra", 0x22, err));
    CHECK(get(s0 + L"\\Terra.spo") == "A1");
    CHECK(get(s0 + L"\\guest-progress.txt") == "the guest's game");
    CHECK(get(s1 + L"\\Terra.spo") == "A2");
    // the next share while the guest still plays s0: s1 is updated, s0 still untouched
    std::wstring s2 = slotfs::pick(games, lower(s0), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(s2) == lower(s1));
}

static void test_slot_match()
{
    printf("world: a slot is reused only for the same host world and replaced entirely (BUG-0087)\n");
    std::wstring games = fresh_dir(L"match") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A"), "Alice", "Terra", 1, err));
    std::wstring b = slotfs::pick(games, L"", "Bob", "Vulcan", "Vulcan.spo", true, note);
    CHECK(lower(a) != lower(b));
    if (lower(a) != lower(b))
        CHECK(wr(b, world("Vulcan", "B"), "Bob", "Vulcan", 2, err));
    CHECK(get(a + L"\\Terra.spo") == "A");
    CHECK(!exists(b + L"\\Terra.spo"));
    // the same host world again: its slot, with no file of the old copy left
    put(a + L"\\Oldplanet.spo", "an old planet of that slot");
    std::wstring a2 = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(a2) == lower(a));
    CHECK(wr(a2, world("Terra", "A3"), "Alice", "Terra", 3, err));
    CHECK(get(a + L"\\Terra.spo") == "A3");
    CHECK(!exists(a + L"\\Oldplanet.spo") || !slotfs::g_write_fix);
}

static void test_slot_write_fails()
{
    printf("world: a world whose files could not all be written is not kept (BUG-0105)\n");
    std::wstring games = fresh_dir(L"wfail") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A"), "Alice", "Terra", 1, err));
    // a file of the slot is held open by another program (antivirus, backup tool)
    HANDLE hold = CreateFileW((a + L"\\stars.db").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    err.clear();
    bool ok = wr(a, world("Terra", "NEW"), "Alice", "Terra", 2, err);
    CloseHandle(hold);
    // Windows refuses to move a folder with a file open in it: the write fails and says why; Wine
    // moves it. Either way the slot holds one whole world, never a mix of both.
    CHECK(ok || !err.empty());
    printf("  held file: write %s (%s)\n", ok ? "succeeded" : "refused", err.c_str());
    CHECK(get(a + L"\\Terra.spo") == get(a + L"\\stars.db"));
    // a file that cannot be created (a name Windows refuses): the world is not written at all
    std::wstring b = slotfs::pick(games, L"", "Bob", "Vulcan", "Vulcan.spo", true, note);
    auto bad = world("Vulcan", "B");
    bad.push_back({"sub/a\x01:b", {1}});
    err.clear();
    CHECK(!wr(b, bad, "Bob", "Vulcan", 3, err));
    CHECK(!exists(b + L"\\sporecoop.txt"));
}

// BUG-0142: a name outside the ANSI codepage survives a write and a read through the profile API;
// a coop.ini edited by hand in UTF-8 (with or without a BOM) is read as UTF-8
static void test_ini_unicode()
{
    std::wstring f = g_tmp + L"\\coop.ini";
    const char* name = "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F \xE5\xA4\xAA\xE9\x83\x8E"; // "Вася 太郎"
    put(f, "; SporeCoop settings\r\n[coop]\r\nname=Player\r\nshare_world=1\r\n");
    CHECK(skini::write_str(f, "coop", "name", name));
    CHECK(skini::read_str(f, "coop", "name", "") == name);
    CHECK(skini::read_int(f, "coop", "share_world", 0) == 1); // the rest of the file is kept
    std::wstring g = g_tmp + L"\\hand.ini";
    put(g, "[coop]\r\nname=J\xC3\xB6rg\r\n"); // UTF-8 without a BOM (Notepad, any Linux editor)
    CHECK(skini::read_str(g, "coop", "name", "") == "J\xC3\xB6rg");
    std::wstring h = g_tmp + L"\\bom.ini";
    put(h, "\xEF\xBB\xBF[coop]\r\nname=\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F\r\n"); // UTF-8 with a BOM
    CHECK(skini::read_str(h, "coop", "name", "") == "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F");
    std::wstring a = g_tmp + L"\\ascii.ini";
    put(a, "[sporekit]\r\nlog_level=1\r\n");
    CHECK(skini::read_str(a, "sporekit", "log_level", "") == "1");
    CHECK(get(a) == "[sporekit]\r\nlog_level=1\r\n"); // ASCII files stay ASCII (tools edit them with sed)
    std::wstring m = g_tmp + L"\\missing.ini"; // a new file gets a non-ASCII value
    CHECK(skini::write_str(m, "coop", "name", name));
    CHECK(skini::read_str(m, "coop", "name", "") == name);
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "--old")) {
        upd::g_old_fix = false;
        upd::g_dup_fix = false;
        slotfs::g_busy_fix = slotfs::g_match_fix = slotfs::g_write_fix = false;
        skini::g_unicode_fix = false;
    }
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    g_tmp = std::wstring(t) + L"sporecoop-win-tests";
    rmtree(g_tmp);
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    test_cleanup_old();
    test_apply_pack();
    test_pending();
    test_slot_busy();
    test_slot_match();
    test_slot_write_fails();
    test_ini_unicode();
    rmtree(g_tmp);
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
