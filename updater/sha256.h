// SHA-256 for the updater (portable: also built into the Linux unit tests)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace upd {

std::string sha256_hex(const uint8_t* p, size_t n);
inline std::string sha256_hex(const std::vector<uint8_t>& v) { return sha256_hex(v.data(), v.size()); }

} // namespace upd
