#include "dbpf.h"

#include <cstring>

namespace dbpf {

static uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put32(std::vector<uint8_t>& v, size_t at, uint32_t x)
{
    v[at] = x & 0xFF;
    v[at + 1] = x >> 8 & 0xFF;
    v[at + 2] = x >> 16 & 0xFF;
    v[at + 3] = x >> 24;
}

bool Package::load(const std::vector<uint8_t>& file)
{
    file_ = file;
    entries_.clear();
    const uint8_t* f = file_.data();
    size_t n = file_.size();
    if (n < 96 || memcmp(f, "DBPF", 4) != 0)
        return false;
    uint32_t count = le32(f + 0x24), isize = le32(f + 0x2C), ioff = le32(f + 0x40);
    if (!ioff)
        ioff = le32(f + 0x28);
    if ((size_t)ioff + isize > n || isize < 4)
        return false;
    const uint8_t* p = f + ioff;
    const uint8_t* end = p + isize;
    uint32_t flags = le32(p);
    p += 4;
    uint32_t ft = 0, fg = 0;
    if (flags & 1) { ft = le32(p); p += 4; }
    if (flags & 2) { fg = le32(p); p += 4; }
    if (flags & 4) p += 4;
    for (uint32_t i = 0; i < count; i++) {
        size_t need = (flags & 1 ? 0 : 4) + (flags & 2 ? 0 : 4) + 4 + 12 + 4;
        if (p + need > end)
            return false;
        Entry e;
        if (flags & 1) e.key.type = ft; else { e.key.type = le32(p); p += 4; }
        if (flags & 2) e.key.group = fg; else { e.key.group = le32(p); p += 4; }
        e.key.instance = le32(p); p += 4;
        e.offset = le32(p);
        e.size = le32(p + 4) & 0x7FFFFFFF;
        e.usize = le32(p + 8);
        e.refpack = (p[12] | p[13] << 8) == 0xFFFF;
        p += 16;
        if ((size_t)e.offset + e.size > n)
            continue;
        entries_.push_back(e);
    }
    return true;
}

const Entry* Package::find(const Key& k) const
{
    for (auto& e : entries_)
        if (e.key == k)
            return &e;
    return nullptr;
}

bool Package::read(const Entry& e, std::vector<uint8_t>& out) const
{
    const uint8_t* s = file_.data() + e.offset;
    if (!e.refpack) {
        out.assign(s, s + e.size);
        return true;
    }
    return refpack_decode(s, e.size, out) && (e.usize == 0 || out.size() == e.usize);
}

// EA RefPack (QFS): 2 flag bytes, 3 or 4 byte big-endian size, then control codes
bool refpack_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out)
{
    out.clear();
    if (n < 5)
        return false;
    size_t o = 2;
    uint32_t size;
    if (src[0] & 0x80) {
        size = be32(src + 2);
        o = 6;
    } else {
        size = (uint32_t)src[2] << 16 | src[3] << 8 | src[4];
        o = 5;
    }
    if (size > (64u << 20))
        return false;
    out.reserve(size);
    auto lit = [&](size_t k) {
        if (o + k > n)
            return false;
        out.insert(out.end(), src + o, src + o + k);
        o += k;
        return true;
    };
    auto copy = [&](size_t off, size_t len) {
        if (off == 0 || off > out.size())
            return false;
        size_t from = out.size() - off;
        for (size_t i = 0; i < len; i++)
            out.push_back(out[from + i]);
        return true;
    };
    while (o < n) {
        uint8_t c0 = src[o];
        if (c0 < 0x80) {
            if (o + 2 > n) return false;
            uint8_t c1 = src[o + 1];
            o += 2;
            if (!lit(c0 & 3) || !copy(((c0 & 0x60) << 3) + c1 + 1, ((c0 & 0x1C) >> 2) + 3)) return false;
        } else if (c0 < 0xC0) {
            if (o + 3 > n) return false;
            uint8_t c1 = src[o + 1], c2 = src[o + 2];
            o += 3;
            if (!lit((c1 & 0xC0) >> 6) || !copy(((c1 & 0x3F) << 8) + c2 + 1, (c0 & 0x3F) + 4)) return false;
        } else if (c0 < 0xE0) {
            if (o + 4 > n) return false;
            uint8_t c1 = src[o + 1], c2 = src[o + 2], c3 = src[o + 3];
            o += 4;
            if (!lit(c0 & 3) || !copy(((c0 & 0x10) << 12) + (c1 << 8) + c2 + 1, ((c0 & 0x0C) << 6) + c3 + 5))
                return false;
        } else if (c0 < 0xFC) {
            o++;
            if (!lit(((c0 & 0x1F) << 2) + 4)) return false;
        } else {
            o++;
            if (!lit(c0 & 3)) return false;
            break;
        }
    }
    return out.size() == size;
}

std::vector<uint8_t> write(const std::vector<Record>& records)
{
    std::vector<uint8_t> f(96, 0);
    memcpy(f.data(), "DBPF", 4);
    put32(f, 4, 3);
    std::vector<uint32_t> offs;
    for (auto& r : records) {
        offs.push_back((uint32_t)f.size());
        f.insert(f.end(), r.data.begin(), r.data.end());
    }
    size_t ioff = f.size();
    std::vector<uint8_t> idx(8 + records.size() * 28, 0);
    put32(idx, 0, 4); // flags: constant "unknown" field (0)
    for (size_t i = 0; i < records.size(); i++) {
        size_t at = 8 + i * 28;
        const Record& r = records[i];
        put32(idx, at, r.key.type);
        put32(idx, at + 4, r.key.group);
        put32(idx, at + 8, r.key.instance);
        put32(idx, at + 12, offs[i]);
        put32(idx, at + 16, (uint32_t)r.data.size() | 0x80000000u);
        put32(idx, at + 20, (uint32_t)r.data.size());
        idx[at + 24] = 0; idx[at + 25] = 0; // not compressed
        idx[at + 26] = 1; idx[at + 27] = 0;
    }
    f.insert(f.end(), idx.begin(), idx.end());
    put32(f, 0x24, (uint32_t)records.size());
    put32(f, 0x2C, (uint32_t)idx.size());
    put32(f, 0x3C, 3);
    put32(f, 0x40, (uint32_t)ioff);
    return f;
}

static std::string utf16le_to_utf8(const uint8_t* p, size_t chars)
{
    std::string s;
    for (size_t i = 0; i < chars; i++) {
        uint32_t c = p[2 * i] | p[2 * i + 1] << 8;
        if (c < 0x80)
            s += (char)c;
        else if (c < 0x800) {
            s += (char)(0xC0 | c >> 6);
            s += (char)(0x80 | (c & 0x3F));
        } else {
            s += (char)(0xE0 | c >> 12);
            s += (char)(0x80 | (c >> 6 & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        }
    }
    return s;
}

// version (BE u32), ..., FF FF FF FF FF FF FF FE, then author and name: BE u32 length + UTF-16LE
bool metadata_names(const std::vector<uint8_t>& m, std::string& author, std::string& name)
{
    static const uint8_t mark[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE};
    for (size_t i = 0; i + 8 <= m.size(); i++) {
        if (memcmp(m.data() + i, mark, 8) != 0)
            continue;
        size_t o = i + 8;
        std::string out[2];
        for (auto& s : out) {
            if (o + 4 > m.size())
                return false;
            uint32_t len = be32(m.data() + o);
            o += 4;
            if (len > 256 || o + len * 2 > m.size())
                return false;
            s = utf16le_to_utf8(m.data() + o, len);
            o += len * 2;
        }
        author = out[0];
        name = out[1];
        return true;
    }
    return false;
}

} // namespace dbpf
