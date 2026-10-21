// Minimal PNG writer (stored/uncompressed deflate). sporectl recompresses on Linux.
#include "core.h"

#include <cstdio>

namespace sk {

static uint32_t crc_table[256];
static void crc_init()
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc(uint32_t c, const uint8_t* b, size_t n)
{
    c ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void be32(std::vector<uint8_t>& v, uint32_t x)
{
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}

static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> h;
    be32(h, (uint32_t)data.size());
    fwrite(h.data(), 1, 4, f);
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    fwrite(td.data(), 1, td.size(), f);
    std::vector<uint8_t> c;
    be32(c, crc(0, td.data(), td.size()));
    fwrite(c.data(), 1, 4, f);
}

bool png_write_rgb(const std::wstring& path, int w, int h, const uint8_t* rgb)
{
    static bool inited = false;
    if (!inited) { crc_init(); inited = true; }
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f)
        return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ihdr;
    be32(ihdr, w); be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0}); // 8-bit RGB
    chunk(f, "IHDR", ihdr);

    // raw scanlines with filter byte 0
    size_t stride = (size_t)w * 3 + 1;
    std::vector<uint8_t> raw(stride * h);
    for (int y = 0; y < h; y++) {
        raw[y * stride] = 0;
        memcpy(&raw[y * stride + 1], rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    // zlib stored blocks
    std::vector<uint8_t> z;
    z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    while (off < raw.size() || raw.empty()) {
        size_t n = raw.size() - off > 65535 ? 65535 : raw.size() - off;
        bool last = off + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(n & 0xFF); z.push_back(n >> 8);
        z.push_back(~n & 0xFF); z.push_back((~n >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
        if (last) break;
    }
    be32(z, (b << 16) | a);
    chunk(f, "IDAT", z);
    chunk(f, "IEND", {});
    bool ok = ferror(f) == 0;
    fclose(f);
    return ok;
}

} // namespace sk
