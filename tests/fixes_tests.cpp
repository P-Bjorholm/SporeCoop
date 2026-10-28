// Host-side unit tests of the fix switch registry (mods/coop/fixes.cpp, BUG-0116): make test.
// `fixes_tests <sources...>` also scans the mod's sources: every COOP_FIX line names a unique
// `<area>.<switch>` and a BUG-NNNN ticket, and every fix bool of the usual shapes (`g_fix_x`,
// `g_x_fix`, `x_fix`) defined `= true` is registered, so a new fix cannot miss `coop.fix`.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "fixes.h"

static int g_fail;
#define CHECK(c)                                                  \
    do {                                                          \
        if (!(c)) {                                               \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            g_fail++;                                             \
        }                                                         \
    } while (0)

static bool g_a = true, g_b = true, g_c = false;
COOP_FIX("t.a", "BUG-0001", g_a, "first test switch");
COOP_FIX("t.b", "BUG-0002", g_b, "second test switch");

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static void test_registry()
{
    CHECK(fix::all().size() == 2);
    CHECK(fix::find("t.a") && fix::find("t.a")->on == &g_a && !strcmp(fix::find("t.a")->ticket, "BUG-0001"));
    CHECK(!fix::find("t.nope"));
    // a duplicate name or a null bool is refused, the first registration stays
    CHECK(!fix::reg("t.a", "BUG-0003", &g_c, "dup"));
    CHECK(!fix::reg("t.c", "BUG-0003", nullptr, "null"));
    CHECK(fix::duplicates() == 2 && fix::all().size() == 2 && fix::find("t.a")->on == &g_a);
    CHECK(has(fix::list(), "2 refused"));
    // set: the module's own bool changes (the old commands see it)
    CHECK(fix::set("t.a", false) && !g_a && g_b);
    CHECK(!fix::set("t.nope", false));
    CHECK(has(fix::list(), "2 fixes (1 off)") && has(fix::list(), "t.a off BUG-0001  first test switch\n"));
    CHECK(fix::set_all(true) == 2 && g_a && g_b);
    CHECK(fix::set_all(false) == 2 && !g_a && !g_b);
    fix::set_all(true);
}

static void test_command()
{
    bool ok;
    CHECK(has(fix::command({}, &ok), "2 fixes (0 off)") && ok);
    CHECK(has(fix::command({"list"}, &ok), "t.b on BUG-0002") && ok);
    CHECK(has(fix::command({"t.b", "off"}, &ok), "t.b off") && ok && !g_b);
    CHECK(has(fix::command({"t.b", "1"}, &ok), "t.b on") && ok && g_b);
    CHECK(has(fix::command({"t.b", "0"}, &ok), "t.b off") && ok && !g_b);
    CHECK(has(fix::command({"t.b"}, &ok), "t.b off") && ok && !g_b); // shows, does not change
    CHECK(has(fix::command({"all", "on"}, &ok), "all 2 fixes on") && ok && g_a && g_b);
    CHECK(has(fix::command({"all", "off"}, &ok), "all 2 fixes off") && ok && !g_a && !g_b);
    CHECK(has(fix::command({"all", "on"}, &ok), "on") && g_a && g_b);
    CHECK(has(fix::command({"nope", "off"}, &ok), "unknown fix 'nope'") && !ok);
    CHECK(has(fix::command({"t.a", "maybe"}, &ok), "usage") && !ok && g_a);
    CHECK(has(fix::command({"all"}, &ok), "usage") && !ok && g_a);
    CHECK(has(fix::command({"t.a", "off", "x"}, &ok), "usage") && !ok && g_a);
}

// the sources of the mod: registrations well formed and unique, no fix bool left out
static void test_sources(int argc, char** argv)
{
    if (argc < 2)
        return;
    std::regex reg_re(R"re(COOP_FIX\("([^"]*)",\s*"([^"]*)",\s*([A-Za-z0-9_:.]+))re");
    std::regex name_re(R"([a-z0-9_]+\.[a-z0-9_]+)"), ticket_re(R"(BUG-\d{4}( BUG-\d{4})*)");
    std::regex decl_re(R"(^\s*(?:static\s+|inline\s+)*bool\s)");
    std::regex def_re(R"(\b(g_fix_[a-z_]+|[a-z_]+_fix|g_fix)\s*=\s*true)");
    std::set<std::string> names, vars;
    std::map<std::string, std::string> defs; // bool name -> file
    int n = 0;
    for (int i = 1; i < argc; i++) {
        std::ifstream f(argv[i]);
        std::stringstream ss;
        ss << f.rdbuf();
        std::string src = ss.str(), line;
        for (std::sregex_iterator it(src.begin(), src.end(), reg_re), end; it != end; ++it) {
            std::string name = (*it)[1], ticket = (*it)[2], var = (*it)[3];
            n++;
            if (!std::regex_match(name, name_re) || !std::regex_match(ticket, ticket_re) || !names.insert(name).second) {
                printf("  FAIL %s: COOP_FIX(\"%s\", \"%s\"): name <area>.<switch>, unique; ticket BUG-NNNN\n",
                       argv[i], name.c_str(), ticket.c_str());
                g_fail++;
            }
            size_t p = var.find_last_of(":.");
            vars.insert(p == std::string::npos ? var : var.substr(p + 1));
            if (p != std::string::npos && var[p] == '.') // net::g_fix.big_reliable: the member counts
                vars.insert(var);
        }
        std::istringstream ls(src);
        while (std::getline(ls, line)) {
                    if (std::regex_search(line, decl_re))
                for (std::sregex_iterator it(line.begin(), line.end(), def_re), end; it != end; ++it)
                    defs[(*it)[1]] = argv[i];
        }
    }
    for (auto& [var, file] : defs)
        if (!vars.count(var)) {
            printf("  FAIL %s: bool %s looks like a fix switch but has no COOP_FIX\n", file.c_str(), var.c_str());
            g_fail++;
        }
    printf("  sources: %d registrations, %zu fix bools defined\n", n, defs.size());
    CHECK(n >= 50);
}

int main(int argc, char** argv)
{
    test_registry();
    test_command();
    test_sources(argc, argv);
    printf("fixes tests: %s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
