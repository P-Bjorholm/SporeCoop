#include "winupdate.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>

#include "sha256.h"

namespace upd {

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

bool http_get(const std::string& url, std::vector<uint8_t>& out, std::string& err, const Progress& progress,
              uint32_t max_bytes)
{
    out.clear();
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        err = "bad address: " + url;
        return false;
    }
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    HINTERNET s = WinHttpOpen(L"SporeCoop-updater", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) {
        err = "no network (WinHTTP)";
        return false;
    }
    WinHttpSetTimeouts(s, 10000, 10000, 15000, 30000);
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    bool ok = false;
    if (r) {
        // GitHub answers the API in JSON and redirects downloads to its file host
        const wchar_t* hdr = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
        if (WinHttpSendRequest(r, hdr, (DWORD)-1, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(r, nullptr)) {
            DWORD status = 0, len = sizeof status;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                &status, &len, WINHTTP_NO_HEADER_INDEX);
            DWORD total = 0;
            len = sizeof total;
            WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &total, &len, WINHTTP_NO_HEADER_INDEX);
            if (status != 200) {
                err = "the server answered " + std::to_string(status) + " for " + url;
            } else {
                ok = true;
                for (;;) {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(r, &avail)) {
                        ok = false;
                        err = "the download broke off";
                        break;
                    }
                    if (avail == 0)
                        break;
                    size_t at = out.size();
                    if (at + avail > max_bytes) {
                        ok = false;
                        err = "the download is too big";
                        break;
                    }
                    out.resize(at + avail);
                    DWORD got = 0;
                    if (!WinHttpReadData(r, out.data() + at, avail, &got)) {
                        ok = false;
                        err = "the download broke off";
                        break;
                    }
                    out.resize(at + got);
                    if (progress)
                        progress(out.size(), total);
                }
            }
        } else
            err = "cannot reach " + narrow(host) + " (error " + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(r);
    } else
        err = "cannot reach " + narrow(host);
    if (c)
        WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

bool latest_release(const std::string& api_url, Release& out, std::string& err)
{
    std::vector<uint8_t> body;
    if (!http_get(api_url, body, err, {}, 4u << 20))
        return false;
    if (!parse_release(std::string(body.begin(), body.end()), out)) {
        err = "no release found";
        return false;
    }
    return true;
}

bool download_pack(const Release& r, Pack& out, std::string& err, const Progress& progress)
{
    if (r.pack_url.empty() || r.sums_url.empty()) {
        err = "the release " + r.tag + " has no update pack";
        return false;
    }
    std::vector<uint8_t> sums, pack;
    if (!http_get(r.sums_url, sums, err, {}, 1u << 20) || !http_get(r.pack_url, pack, err, progress))
        return false;
    std::string name = r.pack_url.substr(r.pack_url.find_last_of('/') + 1);
    std::string want = sums_lookup(std::string(sums.begin(), sums.end()), name);
    if (want.empty() || sha256_hex(pack) != want) {
        err = "the downloaded update does not match the release checksum";
        return false;
    }
    return parse_pack(pack, out, err);
}

bool read_pack_file(const std::wstring& path, Pack& out, std::string& err)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "cannot open " + narrow(path);
        return false;
    }
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<uint8_t> b((size_t)std::min<LONGLONG>(sz.QuadPart, 96 << 20));
    DWORD got = 0;
    bool ok = ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr) && got == b.size();
    CloseHandle(f);
    if (!ok) {
        err = "cannot read " + narrow(path);
        return false;
    }
    return parse_pack(b, out, err);
}

static std::wstring native(const std::wstring& root, const std::string& rel)
{
    std::wstring p = root + L"\\" + widen(rel);
    std::replace(p.begin(), p.end(), L'/', L'\\');
    return p;
}

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void make_dirs(const std::wstring& file)
{
    for (size_t i = 3; i < file.size(); i++)
        if (file[i] == L'\\')
            CreateDirectoryW(file.substr(0, i).c_str(), nullptr);
}

static bool write_all(const std::wstring& p, const std::vector<uint8_t>& d)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    bool ok = WriteFile(f, d.data(), (DWORD)d.size(), &w, nullptr) && w == d.size();
    ok = FlushFileBuffers(f) && ok;
    CloseHandle(f);
    return ok;
}

static std::string why(const char* what, const std::wstring& p)
{
    DWORD e = GetLastError();
    std::string s = std::string(what) + " " + narrow(p);
    if (e == ERROR_ACCESS_DENIED)
        s += " (access denied: run as administrator?)";
    else if (e == ERROR_SHARING_VIOLATION)
        s += " (in use: close Spore?)";
    else
        s += " (error " + std::to_string(e) + ")";
    return s;
}

bool apply_pack(const std::wstring& root, const Pack& p, std::string& err, int* pending)
{
    std::string later; // relative paths that wait for the next start
    // the game's own steam_api.dll is kept as steam_api_orig.dll (the loader forwards to it)
    // before the loader takes its place
    std::wstring sa = root + L"\\SporebinEP1\\steam_api.dll", orig = root + L"\\SporebinEP1\\steam_api_orig.dll";
    if (!exists(orig)) {
        if (!exists(sa) || is_loader(sa)) {
            err = "the game's steam_api.dll is missing: reinstall the game files first";
            return false;
        }
        if (!CopyFileW(sa.c_str(), orig.c_str(), TRUE)) {
            err = why("cannot keep the original", sa);
            return false;
        }
    }
    struct Step {
        std::wstring target, fresh, old;
        std::string rel;
        bool had = false, moved_old = false, placed = false, deferred = false;
    };
    std::vector<Step> steps;
    // 1. every new file next to its target
    for (auto& f : p.files) {
        if (!allowed_path(f.path)) {
            err = "refusing to write " + f.path;
            return false;
        }
        Step s;
        s.rel = f.path;
        s.target = native(root, f.path);
        s.had = exists(s.target);
        if ((f.flags & kOnlyIfMissing) && s.had)
            continue;
        s.fresh = s.target + L".new";
        s.old = s.target + L".old";
        make_dirs(s.target);
        if (!write_all(s.fresh, f.data)) {
            err = why("cannot write", s.fresh);
            for (auto& d : steps)
                DeleteFileW(d.fresh.c_str());
            return false;
        }
        steps.push_back(s);
    }
    // 2. swap them in: the current file becomes *.old (a loaded DLL can be renamed, not overwritten)
    for (auto& s : steps) {
        if (s.had) {
            DeleteFileW(s.old.c_str());
            if (!MoveFileExW(s.target.c_str(), s.old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                if (GetLastError() == ERROR_SHARING_VIOLATION) { // open in the game: next start
                    s.deferred = true;
                    later += s.rel + "\n";
                    continue;
                }
                err = why("cannot replace", s.target);
                goto rollback;
            }
            s.moved_old = true;
        }
        if (!MoveFileExW(s.fresh.c_str(), s.target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            err = why("cannot install", s.target);
            goto rollback;
        }
        s.placed = true;
    }
    if (!later.empty()) {
        std::wstring list = root + L"\\SporebinEP1\\mods\\sporecoop.pending";
        std::vector<uint8_t> old;
        HANDLE f = CreateFileW(list.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) { // keep entries of an earlier update
            char b[4096];
            DWORD got = 0;
            ReadFile(f, b, sizeof b, &got, nullptr);
            CloseHandle(f);
            old.assign(b, b + got);
        }
        old.insert(old.end(), later.begin(), later.end());
        if (!write_all(list, old)) {
            err = why("cannot write", list);
            goto rollback;
        }
    }
    if (pending) {
        *pending = 0;
        for (auto& s : steps)
            *pending += s.deferred;
    }
    return true;
rollback:
    for (auto& s : steps) {
        if (s.placed)
            DeleteFileW(s.target.c_str());
        if (s.moved_old)
            MoveFileExW(s.old.c_str(), s.target.c_str(), MOVEFILE_REPLACE_EXISTING);
        DeleteFileW(s.fresh.c_str());
        s.deferred = false;
    }
    return false;
}

void apply_pending(const std::wstring& root)
{
    std::wstring list = root + L"\\SporebinEP1\\mods\\sporecoop.pending";
    HANDLE f = CreateFileW(list.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    char b[4096];
    DWORD got = 0;
    ReadFile(f, b, sizeof b, &got, nullptr);
    CloseHandle(f);
    std::string text(b, got);
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos)
            e = text.size();
        std::string rel = text.substr(p, e - p);
        while (!rel.empty() && rel.back() == '\r')
            rel.pop_back();
        p = e + 1;
        if (!allowed_path(rel))
            continue;
        std::wstring t = native(root, rel);
        if (exists(t + L".new")) {
            MoveFileExW(t.c_str(), (t + L".old").c_str(), MOVEFILE_REPLACE_EXISTING);
            if (!MoveFileExW((t + L".new").c_str(), t.c_str(), MOVEFILE_REPLACE_EXISTING))
                MoveFileExW((t + L".old").c_str(), t.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    DeleteFileW(list.c_str());
}

void cleanup_old(const std::wstring& root)
{
    for (const wchar_t* dir : {L"\\SporebinEP1", L"\\SporebinEP1\\mods", L"\\SporebinEP1\\mods\\sporekit\\symbols",
                               L"\\Data"}) {
        std::wstring d = root + dir;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((d + L"\\*.old").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            continue;
        do
            DeleteFileW((d + L"\\" + fd.cFileName).c_str()); // still loaded: removed next time
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

std::string installed_version(const std::wstring& root)
{
    HANDLE f = CreateFileW((root + L"\\SporebinEP1\\mods\\sporecoop.version").c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    char b[64] = {};
    DWORD got = 0;
    ReadFile(f, b, sizeof b - 1, &got, nullptr);
    CloseHandle(f);
    std::string v(b, got);
    while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' '))
        v.pop_back();
    return v;
}

bool is_loader(const std::wstring& dll)
{
    HANDLE f = CreateFileW(dll.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    std::vector<char> b(256 << 10);
    DWORD got = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(f);
    static const char tag[] = "sporekit-loader";
    return std::search(b.begin(), b.begin() + got, tag, tag + sizeof tag - 1) != b.begin() + got;
}

} // namespace upd
