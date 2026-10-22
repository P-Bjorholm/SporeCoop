// Spore resource packages (DBPF 3.0, the format of EditorSaves.package and the game packages):
// read the index and records (uncompressed or RefPack), write a small uncompressed package.
// Plain C++ (no game code) so the host build can unit test it (tests/net_tests.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dbpf {

struct Key {
    uint32_t instance = 0, type = 0, group = 0;
    bool operator==(const Key& o) const { return instance == o.instance && type == o.type && group == o.group; }
};

struct Entry {
    Key key;
    uint32_t offset = 0, size = 0, usize = 0;
    bool refpack = false;
};

struct Record {
    Key key;
    std::vector<uint8_t> data; // uncompressed
};

class Package {
public:
    bool load(const std::vector<uint8_t>& file); // whole file in memory
    const std::vector<Entry>& entries() const { return entries_; }
    bool read(const Entry& e, std::vector<uint8_t>& out) const;
    const Entry* find(const Key& k) const;

private:
    std::vector<uint8_t> file_;
    std::vector<Entry> entries_;
};

bool refpack_decode(const uint8_t* src, size_t n, std::vector<uint8_t>& out);
std::vector<uint8_t> write(const std::vector<Record>& records);

// pollen_metadata (0x030BDEE3): author and creation name (UTF-8), false if not understood
bool metadata_names(const std::vector<uint8_t>& meta, std::string& author, std::string& name);

} // namespace dbpf
