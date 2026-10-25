// A small ZIP writer for the problem report archive (diag.cpp): stored entries (no compression:
// the archive is a few MB of logs and small minidumps), CRC-32, DOS times, UTF-8 names.
#include "core.h"

#include <cstdio>

namespace sk {

static uint32_t crc32(const uint8_t* p, size_t n)
{
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void put16(std::vector<uint8_t>& o, uint32_t v)
{
    o.push_back((uint8_t)v);
    o.push_back((uint8_t)(v >> 8));
}
static void put32(std::vector<uint8_t>& o, uint32_t v)
{
    put16(o, v & 0xFFFF);
    put16(o, v >> 16);
}

bool zip_write(const std::wstring& path, const std::vector<ZipEntry>& entries)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    uint32_t dtime = (t.wHour << 11) | (t.wMinute << 5) | (t.wSecond / 2);
    uint32_t ddate = ((t.wYear - 1980) << 9) | (t.wMonth << 5) | t.wDay;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    std::vector<uint8_t> central;
    uint32_t offset = 0;
    bool ok = true;
    auto write = [&](const void* p, size_t n) {
        DWORD w = 0;
        ok = ok && WriteFile(f, p, (DWORD)n, &w, nullptr) && w == n;
        offset += (uint32_t)n;
    };
    for (auto& e : entries) {
        uint32_t crc = crc32(e.data.data(), e.data.size()), size = (uint32_t)e.data.size();
        std::vector<uint8_t> h;
        put32(h, 0x04034B50); // local file header
        put16(h, 20);         // version needed
        put16(h, 0x0800);     // flags: UTF-8 names
        put16(h, 0);          // stored
        put16(h, dtime);
        put16(h, ddate);
        put32(h, crc);
        put32(h, size);
        put32(h, size);
        put16(h, (uint32_t)e.name.size());
        put16(h, 0);
        h.insert(h.end(), e.name.begin(), e.name.end());
        uint32_t at = offset;
        write(h.data(), h.size());
        write(e.data.data(), e.data.size());
        put32(central, 0x02014B50); // central directory header
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, dtime);
        put16(central, ddate);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, (uint32_t)e.name.size());
        put16(central, 0); // extra
        put16(central, 0); // comment
        put16(central, 0); // disk
        put16(central, 0); // internal attributes
        put32(central, 0); // external attributes
        put32(central, at);
        central.insert(central.end(), e.name.begin(), e.name.end());
    }
    uint32_t cd_at = offset;
    write(central.data(), central.size());
    std::vector<uint8_t> end;
    put32(end, 0x06054B50);
    put16(end, 0);
    put16(end, 0);
    put16(end, (uint32_t)entries.size());
    put16(end, (uint32_t)entries.size());
    put32(end, (uint32_t)central.size());
    put32(end, cd_at);
    put16(end, 0);
    write(end.data(), end.size());
    CloseHandle(f);
    if (!ok)
        DeleteFileW(path.c_str());
    return ok;
}

// a file's bytes (at most the last `cap` bytes; a log is written while we read it: share all)
bool read_file_tail(const std::wstring& path, size_t cap, std::vector<uint8_t>& out)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    uint64_t n = (uint64_t)sz.QuadPart, from = n > cap ? n - cap : 0;
    LARGE_INTEGER pos;
    pos.QuadPart = (LONGLONG)from;
    SetFilePointerEx(f, pos, nullptr, FILE_BEGIN);
    out.resize((size_t)(n - from));
    DWORD got = 0;
    BOOL ok = out.empty() || ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr);
    out.resize(got);
    CloseHandle(f);
    return ok;
}

} // namespace sk
