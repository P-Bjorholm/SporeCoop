// SporeCoop update pack ("SporeCoop-<version>.pack", built by tools/mkpack.py, attached to every
// GitHub release): the files of one version with their SHA-256, paths relative to the game folder.
// Portable (no Win32): parsed and checked by the mod, the installer and the Linux unit tests.
//
//   "SCUP" u32 format(1) str16 version u32 count
//   count x { str16 path ('/' separated) u8 flags u32 size  32 bytes sha256 }
//   the file data, in entry order
//
// flags: 1 = install only when the file is missing (default configs: never overwrite the user's)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace upd {

struct PackFile {
    std::string path; // e.g. "SporebinEP1/mods/coop.dll"
    uint8_t flags = 0;
    std::vector<uint8_t> data;
};
constexpr uint8_t kOnlyIfMissing = 1;

struct Pack {
    std::string version;
    std::vector<PackFile> files;
};

// The only places an update may write to (a pack comes from the internet): the loader, SporeKit,
// the mods folder and the resource package. No "..", no absolute paths, no drive letters.
bool allowed_path(const std::string& path);

// parse + check every file's SHA-256 and path; err says what is wrong
bool parse_pack(const std::vector<uint8_t>& bytes, Pack& out, std::string& err);
std::vector<uint8_t> write_pack(const Pack& p); // tests (the release pack is made by tools/mkpack.py)

// "0.5.1" vs "0.5.0": -1, 0, 1 (a leading "v" is ignored; missing parts are 0)
int compare_versions(const std::string& a, const std::string& b);

// the latest release from the GitHub API answer (/repos/<owner>/<repo>/releases/latest)
struct Release {
    std::string tag, version;          // "v0.5.1", "0.5.1"
    std::string pack_url, sums_url;    // update pack, SHA256SUMS.txt
    std::string setup_url, page_url;   // SporeCoopSetup.exe, the release page
};
bool parse_release(const std::string& json, Release& out);

// the hash of `file` in a SHA256SUMS.txt ("<hex>  <name>" lines), "" if absent
std::string sums_lookup(const std::string& sums, const std::string& file);

} // namespace upd
