// The registry of the bug fixes' test switches (fixes.h, BUG-0116).
#include "fixes.h"

#include <cstring>

namespace fix {

// function-local statics: modules register from their static initialisers, in any order
static std::vector<Info>& table()
{
    static std::vector<Info> t;
    return t;
}
static int& dup_count()
{
    static int n = 0;
    return n;
}

bool reg(const char* name, const char* ticket, bool* on, const char* what)
{
    if (!name || !*name || !on || find(name)) {
        dup_count()++;
        return false;
    }
    table().push_back({name, ticket ? ticket : "", on, what ? what : ""});
    return true;
}

const std::vector<Info>& all() { return table(); }
int duplicates() { return dup_count(); }

const Info* find(const std::string& name)
{
    for (const Info& f : table())
        if (name == f.name)
            return &f;
    return nullptr;
}

bool set(const std::string& name, bool on)
{
    const Info* f = find(name);
    if (!f)
        return false;
    *f->on = on;
    return true;
}

size_t set_all(bool on)
{
    for (const Info& f : table())
        *f.on = on;
    return table().size();
}

static std::string line(const Info& f)
{
    return std::string(f.name) + (*f.on ? " on " : " off ") + f.ticket + "  " + f.what + "\n";
}

std::string list()
{
    size_t off = 0;
    for (const Info& f : table())
        off += !*f.on;
    std::string s = std::to_string(table().size()) + " fixes (" + std::to_string(off) + " off)";
    if (dup_count())
        s += ", " + std::to_string(dup_count()) + " refused registrations (duplicate name)";
    s += "\n";
    for (const Info& f : table())
        s += line(f);
    return s;
}

static int parse_state(const std::string& v)
{
    if (v == "on" || v == "1")
        return 1;
    if (v == "off" || v == "0")
        return 0;
    return -1;
}

std::string command(const std::vector<std::string>& a, bool* ok)
{
    if (ok)
        *ok = true;
    const char* usage = "usage: coop.fix [list] | <name> [on|off] | all on|off  (0 / off = the code before the fix, tests)\n";
    if (a.empty() || a[0] == "list")
        return list();
    int v = a.size() >= 2 ? parse_state(a[1]) : -2;
    if (a.size() > 2 || v == -1) {
        if (ok)
            *ok = false;
        return usage;
    }
    if (a[0] == "all") {
        if (v < 0) {
            if (ok)
                *ok = false;
            return usage;
        }
        size_t n = set_all(v == 1);
        return "all " + std::to_string(n) + " fixes " + (v ? "on" : "off") + "\n";
    }
    const Info* f = find(a[0]);
    if (!f) {
        if (ok)
            *ok = false;
        return "unknown fix '" + a[0] + "' (coop.fix list)\n";
    }
    if (v >= 0)
        *f->on = v == 1;
    return line(*f);
}

} // namespace fix
