#include "install.h"

#include <windows.h>

#include <vector>

namespace inst {

bool g_fix = true;

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool is_game(const std::wstring& root) { return !root.empty() && exists(root + L"\\SporebinEP1\\SporeApp.exe"); }

std::wstring local_pack(const Options& opt)
{
    if (!opt.pack.empty())
        return opt.pack;
    std::vector<std::string> names;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((opt.pack_dir + L"\\SporeCoop-*.pack").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            names.push_back(upd::narrow(fd.cFileName));
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::string n = g_fix ? upd::pick_pack(names) : names.empty() ? std::string() : names[0]; // old: the first listed
    return n.empty() ? std::wstring() : opt.pack_dir + L"\\" + upd::widen(n);
}

Result do_install(const std::wstring& root, const Options& opt, upd::ReleaseBox& box, std::string& msg,
                  const upd::Progress& progress, std::string* older_version, std::string* installed)
{
    if (!is_game(root)) {
        msg = "SporeApp.exe is not in " + upd::narrow(root) + "\\SporebinEP1";
        return Result::Failed;
    }
    upd::apply_pending(root); // an in-game update left files for the next start
    upd::Pack pack;
    std::string err;
    std::wstring lp = local_pack(opt);
    bool have_local = !lp.empty() && upd::read_pack_file(lp, pack, err);
    upd::ReleaseInfo ri = box.get(); // BUG-0077: a copy; the check thread may set the box meanwhile
    if (!have_local || (ri.ok && upd::compare_versions(ri.rel.version, pack.version) > 0)) {
        if (!ri.ok) {
            if (!upd::latest_release(opt.api, ri.rel, err)) {
                msg = "cannot get SporeCoop from GitHub: " + err;
                return Result::Failed;
            }
            ri.ok = ri.done = true;
            ri.err.clear();
            box.set(ri);
        }
        if (!upd::download_pack(ri.rel, pack, err, progress)) {
            msg = "download failed: " + err;
            return Result::Failed;
        }
    }
    // BUG-0094: never an older version over a newer one without the player's yes
    std::string have = upd::installed_version(root);
    if (g_fix && !opt.downgrade && !have.empty() && upd::compare_versions(pack.version, have) < 0) {
        if (older_version)
            *older_version = pack.version;
        if (installed)
            *installed = have;
        msg = "SporeCoop " + have + " is installed and the setup has only the older " + pack.version +
              ": nothing changed (/downgrade installs it anyway)";
        return Result::Older;
    }
    if (!upd::apply_pack(root, pack, err)) {
        msg = err;
        return Result::Failed;
    }
    upd::cleanup_old(root);
    msg = "SporeCoop " + pack.version + " is installed. Start Spore: the galaxy menu has a Co-op item.";
    return Result::Ok;
}

bool do_uninstall(const std::wstring& root, std::string& msg)
{
    upd::apply_pending(root);
    std::wstring bin = root + L"\\SporebinEP1";
    std::wstring sa = bin + L"\\steam_api.dll", orig = bin + L"\\steam_api_orig.dll";
    if (exists(orig) && upd::is_loader(sa)) {
        std::wstring old = sa + SC_OLD_SUFFIX;
        DeleteFileW(old.c_str());
        bool moved = MoveFileExW(sa.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (moved && upd::g_swap_delay_ms > 0) // tests (BUG-0139): a wide window between the two moves
            Sleep((DWORD)upd::g_swap_delay_ms);
        if (!moved || !MoveFileExW(orig.c_str(), sa.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            msg = "cannot restore steam_api.dll (run as administrator? is Spore closed?)";
            return false;
        }
    }
    for (const wchar_t* f : {L"\\sporekit.dll", L"\\mods\\coop.dll", L"\\mods\\sporecoop.version"})
        if (exists(bin + f) && !DeleteFileW((bin + f).c_str()))
            MoveFileExW((bin + f).c_str(), (bin + f + SC_OLD_SUFFIX).c_str(), MOVEFILE_REPLACE_EXISTING); // in use
    WIN32_FIND_DATAW fd;
    std::wstring sym = bin + L"\\mods\\sporekit\\symbols";
    HANDLE h = FindFirstFileW((sym + L"\\*.sym").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            DeleteFileW((sym + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    DeleteFileW((root + L"\\Data\\SporeCoop.package").c_str());
    upd::cleanup_old(root);
    msg = "SporeCoop is removed (your settings and logs in SporebinEP1\\mods are kept).";
    return true;
}

} // namespace inst
