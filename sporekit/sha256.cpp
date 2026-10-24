// Compact SHA-256 (public domain style implementation) used to identify the exe build.
#include "core.h"

#include <cstdio>
#include <cstring>

namespace {

struct Sha256 {
    uint32_t h[8];
    uint64_t len = 0;
    uint8_t buf[64];
    size_t n = 0;

    Sha256()
    {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        for (int i = 0; i < 8; i++)
            h[i] = init[i];
    }
    static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }
    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
            0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
            0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
            0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
            0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
            0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
            0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
            0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
            0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
                   (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t* p, size_t sz)
    {
        len += sz;
        while (sz) {
            size_t t = 64 - n < sz ? 64 - n : sz;
            memcpy(buf + n, p, t);
            n += t; p += t; sz -= t;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(uint8_t out[32])
    {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t z = 0;
        while (n != 56)
            update(&z, 1);
        uint8_t lb[8];
        for (int i = 0; i < 8; i++)
            lb[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(lb, 8);
        for (int i = 0; i < 8; i++) {
            out[i * 4] = h[i] >> 24; out[i * 4 + 1] = h[i] >> 16;
            out[i * 4 + 2] = h[i] >> 8; out[i * 4 + 3] = h[i];
        }
    }
};

} // namespace

namespace sk {

static std::string hex_digest(Sha256& s)
{
    uint8_t d[32];
    s.final(d);
    char hex[65];
    for (int i = 0; i < 32; i++)
        snprintf(hex + i * 2, 3, "%02x", d[i]);
    return hex;
}

// code_sha256 of a PE file: SHA-256 over ImageBase (u32 LE), AddressOfEntryPoint (u32 LE), then for
// every section in table order except ".rsrc": Name (8 bytes), VirtualAddress (u32 LE),
// VirtualSize (u32 LE) and the raw bytes [PointerToRawData, + min(VirtualSize, SizeOfRawData)).
// It ignores the PE header (timestamp, checksum), resources and data appended after the image
// (signatures): two exes with the same code and data at the same addresses get the same value.
// The same definition is in tools/exe_info.py and installer/setup.cpp.
std::string pe_code_sha256_hex(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return {};
    std::vector<uint8_t> d;
    {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n > 0) {
            d.resize((size_t)n);
            d.resize(fread(d.data(), 1, d.size(), f));
        }
    }
    fclose(f);
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= d.size() ? d[o] | d[o + 1] << 8 : 0; };
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? u16(o) | u16(o + 2) << 16 : 0; };
    if (d.size() < 0x40 || u16(0) != 0x5A4D)
        return {};
    size_t pe = u32(0x3C);
    if (u32(pe) != 0x00004550 || u16(pe + 24) != 0x10B) // "PE\0\0", PE32
        return {};
    size_t nsec = u16(pe + 6), opt = pe + 24, sec = opt + u16(pe + 20);
    Sha256 s;
    uint8_t w[8];
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++)
            w[i] = (uint8_t)(v >> (8 * i));
        s.update(w, 4);
    };
    put32(u32(opt + 28)); // ImageBase
    put32(u32(opt + 16)); // AddressOfEntryPoint
    for (size_t i = 0; i < nsec; i++) {
        size_t o = sec + i * 40;
        if (o + 40 > d.size())
            return {};
        if (memcmp(&d[o], ".rsrc\0\0\0", 8) == 0)
            continue;
        uint32_t vsize = u32(o + 8), va = u32(o + 12), rsize = u32(o + 16), raw = u32(o + 20);
        uint32_t n = vsize < rsize ? vsize : rsize;
        if ((size_t)raw + n > d.size())
            return {};
        s.update(&d[o], 8);
        put32(va);
        put32(vsize);
        s.update(d.data() + raw, n);
    }
    return hex_digest(s);
}

// Packed exes (the Steam build's SteamStub DRM: .text encrypted in the file, a .bind section with
// the unpacker, the entry point moved there) keep every other section of the build they were made
// from: "data_sha256" identifies the build from the file (ImageBase and every section but .text,
// .bind and .rsrc, in table order: name, VirtualAddress, VirtualSize, raw bytes) and "text_sha256"
// (the .text bytes, relocations undone) proves the unpacked code in memory is that build's code.
std::string pe_data_sha256_hex(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return {};
    std::vector<uint8_t> d;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len > 0) {
        d.resize((size_t)len);
        d.resize(fread(d.data(), 1, d.size(), f));
    }
    fclose(f);
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= d.size() ? d[o] | d[o + 1] << 8 : 0; };
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? u16(o) | u16(o + 2) << 16 : 0; };
    if (d.size() < 0x40 || u16(0) != 0x5A4D)
        return {};
    size_t pe = u32(0x3C);
    if (u32(pe) != 0x00004550 || u16(pe + 24) != 0x10B)
        return {};
    size_t nsec = u16(pe + 6), opt = pe + 24, sec = opt + u16(pe + 20);
    Sha256 s;
    uint8_t w[4];
    auto put32 = [&](uint32_t v) {
        for (int i = 0; i < 4; i++)
            w[i] = (uint8_t)(v >> (8 * i));
        s.update(w, 4);
    };
    put32(u32(opt + 28)); // ImageBase
    for (size_t i = 0; i < nsec; i++) {
        size_t o = sec + i * 40;
        if (o + 40 > d.size())
            return {};
        if (!memcmp(&d[o], ".rsrc\0\0\0", 8) || !memcmp(&d[o], ".text\0\0\0", 8) || !memcmp(&d[o], ".bind\0\0\0", 8))
            continue;
        uint32_t vsize = u32(o + 8), va = u32(o + 12), rsize = u32(o + 16), raw = u32(o + 20);
        uint32_t n = vsize < rsize ? vsize : rsize;
        if ((size_t)raw + n > d.size())
            return {};
        s.update(&d[o], 8);
        put32(va);
        put32(vsize);
        s.update(d.data() + raw, n);
    }
    return hex_digest(s);
}

// SHA-256 of the main module's .text in memory as the file of the unpacked build has it:
// min(VirtualSize, SizeOfRawData) bytes, every HIGHLOW relocation inside it moved back to the
// preferred base. "" when the module has no .text
std::string mem_text_sha256_hex(HMODULE mod, uintptr_t preferred_base)
{
    auto* base = (uint8_t*)mod;
    auto* dos = (IMAGE_DOS_HEADER*)base;
    auto* nt = (IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    uint32_t va = 0, n = 0;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++)
        if (!memcmp(sec[i].Name, ".text\0\0\0", 8)) {
            va = sec[i].VirtualAddress;
            n = sec[i].Misc.VirtualSize < sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        }
    if (!va || !n || IsBadReadPtr(base + va, n))
        return {};
    std::vector<uint8_t> t(base + va, base + va + n);
    uint32_t delta = (uint32_t)((uintptr_t)base - preferred_base);
    auto& rd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (delta && rd.VirtualAddress && rd.Size) {
        uint8_t* p = base + rd.VirtualAddress;
        uint8_t* end = p + rd.Size;
        while (p + 8 <= end) {
            auto* b = (IMAGE_BASE_RELOCATION*)p;
            if (b->SizeOfBlock < 8)
                break;
            auto* e = (uint16_t*)(p + 8);
            for (uint32_t k = 0; k < (b->SizeOfBlock - 8) / 2; k++) {
                uint32_t rva = b->VirtualAddress + (e[k] & 0xFFF);
                if ((e[k] >> 12) == IMAGE_REL_BASED_HIGHLOW && rva >= va && rva + 4 <= va + n) {
                    uint32_t v;
                    memcpy(&v, &t[rva - va], 4);
                    v -= delta;
                    memcpy(&t[rva - va], &v, 4);
                }
            }
            p += b->SizeOfBlock;
        }
    }
    Sha256 s;
    s.update(t.data(), t.size());
    return hex_digest(s);
}

std::string sha256_file_hex(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f)
        return {};
    Sha256 s;
    std::vector<uint8_t> buf(1 << 20);
    size_t r;
    while ((r = fread(buf.data(), 1, buf.size(), f)) > 0)
        s.update(buf.data(), r);
    fclose(f);
    return hex_digest(s);
}

} // namespace sk
