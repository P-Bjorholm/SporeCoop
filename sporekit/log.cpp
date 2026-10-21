#include "core.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

namespace sk {

int g_log_level = SK_DEBUG;

static std::mutex g_mu;
static FILE* g_file = nullptr;
static std::deque<std::string> g_ring;
static const size_t kRing = 2000;

void log_init(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file)
        fclose(g_file);
    g_file = _wfopen(path.c_str(), L"w");
}

void logv(SkMod* mod, int level, const char* fmt, va_list ap)
{
    if (level < g_log_level)
        return;
    char msg[2048];
    vsnprintf(msg, sizeof msg, fmt, ap);
    static const char* lv[] = {"DBG", "INF", "WRN", "ERR"};
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[2300];
    snprintf(line, sizeof line, "%02d:%02d:%02d.%03d [%s] %s: %s", st.wHour, st.wMinute,
             st.wSecond, st.wMilliseconds, lv[level & 3], mod ? mod->name.c_str() : "?", msg);
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_file) {
        fputs(line, g_file);
        fputc('\n', g_file);
        fflush(g_file);
    }
    g_ring.emplace_back(line);
    if (g_ring.size() > kRing)
        g_ring.pop_front();
}

void logf(SkMod* mod, int level, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logv(mod, level, fmt, ap);
    va_end(ap);
}

std::vector<std::string> log_tail(size_t n)
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<std::string> out;
    size_t start = g_ring.size() > n ? g_ring.size() - n : 0;
    for (size_t i = start; i < g_ring.size(); i++)
        out.push_back(g_ring[i]);
    return out;
}

// ---- small utils ----
std::string narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::wstring to_windows_path(const std::string& p)
{
    std::wstring w = widen(p);
    if (!w.empty() && w[0] == L'/')
        w = L"Z:" + w;
    for (auto& c : w)
        if (c == L'/')
            c = L'\\';
    return w;
}

std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 16);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\r': o += "\\r"; break;
        case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) {
                char b[8];
                snprintf(b, sizeof b, "\\u%04x", c);
                o += b;
            } else
                o += (char)c;
        }
    }
    return o;
}

double now_seconds()
{
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER x;
        QueryPerformanceFrequency(&x);
        return x;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

} // namespace sk
