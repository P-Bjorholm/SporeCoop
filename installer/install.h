// The install and uninstall steps of SporeCoopSetup.exe, without the window: used by the setup
// (window and silent mode) and by the Windows unit tests (tests/win_tests.cpp).
#pragma once

#include <functional>
#include <string>

#include "../updater/winupdate.h"

namespace inst {

struct Options {
    std::wstring pack;          // /pack: a pack file to use; empty = SporeCoop-*.pack in pack_dir
    std::wstring pack_dir;      // the setup's own folder
    std::string api = upd::kLatestUrl;
    bool downgrade = false;     // /downgrade or the player said yes: a version older than the installed one is fine
};

bool is_game(const std::wstring& root); // root\SporebinEP1\SporeApp.exe exists

// the pack the setup would use offline: opt.pack, else the newest SporeCoop-*.pack in opt.pack_dir
// (BUG-0094: it was the first one the file system listed)
std::wstring local_pack(const Options& opt);

enum class Result { Ok, Failed, Older };
// Older: the pack is older than the installed version and !opt.downgrade (nothing changed);
// *older_version / *installed tell which (BUG-0094). rel: the release check's result; when it has
// none yet, do_install asks GitHub itself into its own copy and stores it there (BUG-0077).
Result do_install(const std::wstring& root, const Options& opt, upd::ReleaseBox& rel, std::string& msg,
                  const upd::Progress& progress = {}, std::string* older_version = nullptr,
                  std::string* installed = nullptr);
bool do_uninstall(const std::wstring& root, std::string& msg);

// tests: false = the setup's behaviour before BUG-0094 (any pack installed, no downgrade check) and
// BUG-0139 (the window closes during a job); `/oldfix` on the setup's command line
extern bool g_fix;

} // namespace inst
