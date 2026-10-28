#define WIN32_LEAN_AND_MEAN
#include "inifile.h"

#include <windows.h>

#include <mutex>
#include <set>
#include <vector>

namespace skini {

bool g_unicode_fix = true;

static std::wstring wide(const std::string& s, UINT cp = CP_UTF8, DWORD flags = 0)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring w(n, 0);
    MultiByteToWideChar(cp, flags, s.data(), (int)s.size(), &w[0], n);
    return w;
}

static std::string utf8(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n : 0, 0);
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static bool ascii(const std::string& s)
{
    for (unsigned char c : s)
        if (c >= 0x80)
            return false;
    return true;
}

static bool read_all(const std::wstring& path, std::string& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    if (sz.QuadPart > (1 << 20)) { // not a settings file: leave it alone
        CloseHandle(f);
        return false;
    }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    BOOL ok = out.empty() || ReadFile(f, &out[0], (DWORD)out.size(), &got, nullptr);
    out.resize(got);
    CloseHandle(f);
    return ok != FALSE;
}

static std::mutex g_mu;
static std::set<std::wstring> g_unicode, g_ascii; // checked in this run

bool make_unicode(const std::wstring& file, bool force)
{
    if (!g_unicode_fix)
        return false;
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_unicode.count(file) || (!force && g_ascii.count(file)))
        return g_unicode.count(file) != 0;
    std::string d;
    bool have = read_all(file, d);
    if (!have && GetFileAttributesW(file.c_str()) != INVALID_FILE_ATTRIBUTES)
        return false; // there but unreadable (or too big)
    if (d.size() >= 2 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xFE) {
        g_unicode.insert(file);
        return true;
    }
    if (!force && ascii(d)) {
        g_ascii.insert(file);
        return false;
    }
    if (d.size() >= 3 && (unsigned char)d[0] == 0xEF && (unsigned char)d[1] == 0xBB && (unsigned char)d[2] == 0xBF)
        d.erase(0, 3);
    std::wstring w;
    if (!d.empty()) {
        // valid UTF-8 (every editor of today) or else the ANSI codepage (what the profile API wrote)
        bool is_utf8 = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, d.data(), (int)d.size(), nullptr, 0) > 0;
        w = wide(d, is_utf8 ? CP_UTF8 : CP_ACP);
    }
    // a temp file next to it, then a rename: a crash in between never leaves half a file
    std::wstring tmp = file + L".sporecoop-tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD n = 0;
    const unsigned char bom[2] = {0xFF, 0xFE};
    BOOL ok = WriteFile(f, bom, 2, &n, nullptr);
    if (ok && !w.empty())
        ok = WriteFile(f, w.data(), (DWORD)(w.size() * sizeof(wchar_t)), &n, nullptr);
    CloseHandle(f);
    if (!ok || !MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    g_ascii.erase(file);
    g_unicode.insert(file);
    return true;
}

std::string read_str(const std::wstring& file, const char* section, const char* key, const char* def)
{
    make_unicode(file, false); // a hand-edited UTF-8 file is converted before it is read
    std::vector<wchar_t> buf(1024);
    GetPrivateProfileStringW(wide(section).c_str(), wide(key).c_str(), wide(def ? def : "").c_str(), buf.data(),
                             (DWORD)buf.size(), file.c_str());
    return utf8(buf.data());
}

int read_int(const std::wstring& file, const char* section, const char* key, int def)
{
    return (int)GetPrivateProfileIntW(wide(section).c_str(), wide(key).c_str(), def, file.c_str());
}

bool write_str(const std::wstring& file, const char* section, const char* key, const char* value)
{
    if (value && !ascii(value))
        make_unicode(file, true);
    return WritePrivateProfileStringW(wide(section).c_str(), wide(key).c_str(), value ? wide(value).c_str() : nullptr,
                                      file.c_str()) != FALSE;
}

std::string to_utf8_text(const std::string& d)
{
    if (d.size() >= 2 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xFE)
        return utf8(std::wstring((const wchar_t*)(d.data() + 2), (d.size() - 2) / 2));
    return d;
}

} // namespace skini
