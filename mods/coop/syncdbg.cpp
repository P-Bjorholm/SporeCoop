#include "syncdbg.h"

#include <cstdio>

namespace syncdbg {

std::string format(const std::vector<Entry>& v)
{
    std::string s;
    char b[160];
    for (auto& e : v) {
        snprintf(b, sizeof b, "%s %.3f %.3f %.3f %.4f %.4f %.4f %.4f\n", e.key.c_str(), e.pos.x, e.pos.y, e.pos.z,
                 e.rot.x, e.rot.y, e.rot.z, e.rot.w);
        s += b;
    }
    return s;
}

} // namespace syncdbg
