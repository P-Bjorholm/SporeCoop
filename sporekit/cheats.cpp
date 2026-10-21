// Bridge between Spore's cheat console (Ctrl+Shift+C) and devkit commands.
//
// A cheat keyword is backed by a fake ArgScript::ICommand object whose vtable we build
// by hand (MSVC layout: ParseLine, GetDescription, SetData, AddRef, Release, ~dtor).
//   "sk <devkit command line>"        runs any devkit command, e.g. sk coop.status
//   "<keyword> <sub> args"            mod-registered alias, e.g. coop host -> coop.host
#include "core.h"

#include "../sdk/include/sporekit_game.hpp"

using namespace skg;

namespace sk {

struct FakeCommand {
    void** vtbl;
    void* format_parser;
    int refcount;
    void* data;
    // ---- ours ----
    char keyword[32];
    char prefix[32]; // devkit command prefix, "" for the generic "sk" cheat
    char desc[256];
};

static void console_print(const std::string& text)
{
    uintptr_t get = sym("App::ICheatManager::Get");
    uintptr_t printf_ = sym("ArgScript::PrintF");
    if (!get || !printf_)
        return;
    void* cm = cdecl_call<void*>(get);
    if (!cm)
        return;
    void* fp = vcall<void*>(cm, 0x38 / 4); // GetArgScript()
    if (!fp)
        return;
    size_t s = 0;
    while (s < text.size()) { // PrintF formats into a 1 KB buffer: send in lines
        size_t e = text.find('\n', s);
        if (e == std::string::npos)
            e = text.size();
        std::string line = text.substr(s, std::min<size_t>(e - s, 900));
        cdecl_call<void>(printf_, fp, "%s", line.c_str());
        s = e + 1;
    }
}

// ArgScript::Line: vector<char*> mArguments at +0x14, mFirstArgIndex +0x3C, mArgumentCount +0x40
static std::vector<std::string> line_args(const void* line)
{
    std::vector<std::string> out;
    char** begin = field<char**>((void*)line, 0x14);
    char** end = field<char**>((void*)line, 0x18);
    if (!readable(begin, 4) || end < begin || end - begin > 64)
        return out;
    for (char** p = begin; p < end; p++)
        if (*p && readable(*p, 1))
            out.emplace_back(*p);
    return out;
}

static void SK_THISCALL fc_ParseLine(FakeCommand* self, const void* line)
{
    auto args = line_args(line);
    // args[0] is the cheat keyword itself
    std::string cmd;
    size_t first = (!args.empty() && args[0] == self->keyword) ? 1 : 0;
    if (self->prefix[0]) {
        if (args.size() <= first) {
            console_print(self->desc);
            return;
        }
        if (args[first] == "help") {
            std::string out = std::string(self->desc) + "\n";
            command_exec_line(std::string("help ") + self->prefix, out, true);
            console_print(out);
            return;
        }
        cmd = std::string(self->prefix) + args[first];
        first++;
    }
    for (size_t i = first; i < args.size(); i++) {
        if (!cmd.empty())
            cmd += ' ';
        bool q = args[i].find(' ') != std::string::npos;
        cmd += q ? "\"" + args[i] + "\"" : args[i];
    }
    if (cmd.empty()) {
        console_print(self->desc);
        return;
    }
    std::string out;
    command_exec_line(cmd, out, true);
    SKLOG(SK_INFO, "cheat console: %s", cmd.c_str());
    console_print(out.empty() ? "ok" : out);
}

static const char* SK_THISCALL fc_GetDescription(FakeCommand* self, int) { return self->desc; }
static void SK_THISCALL fc_SetData(FakeCommand* self, void* fp, void* data)
{
    self->format_parser = fp;
    self->data = data;
}
static int SK_THISCALL fc_AddRef(FakeCommand* self) { return ++self->refcount; }
static int SK_THISCALL fc_Release(FakeCommand* self) { return --self->refcount; } // static storage
static void* SK_THISCALL fc_Dtor(FakeCommand* self, int) { return self; }

static void* g_vtbl[6] = {(void*)fc_ParseLine, (void*)fc_GetDescription, (void*)fc_SetData,
                          (void*)fc_AddRef,    (void*)fc_Release,        (void*)fc_Dtor};

static std::vector<FakeCommand*> g_cheats;     // registered with the game
static std::vector<FakeCommand*> g_pending;    // waiting for the cheat manager

static bool add_to_game(FakeCommand* fc)
{
    uintptr_t get = sym("App::ICheatManager::Get");
    void* cm = get ? cdecl_call<void*>(get) : nullptr;
    if (!cm)
        return false;
    vcall<void>(cm, 0x18 / 4, fc->keyword, fc, 0); // AddCheat(keyword, ICommand*, notify)
    g_cheats.push_back(fc);
    SKLOG(SK_INFO, "cheat '%s' registered", fc->keyword);
    return true;
}

int cheat_register(const char* keyword, const char* devkit_prefix, const char* desc)
{
    for (auto* c : g_cheats)
        if (!strcmp(c->keyword, keyword))
            return 0; // survives mod hot reloads: the alias just forwards to devkit commands
    for (auto* c : g_pending)
        if (!strcmp(c->keyword, keyword))
            return 0;
    auto* fc = new FakeCommand{};
    fc->vtbl = g_vtbl;
    fc->refcount = 1;
    strncpy(fc->keyword, keyword, sizeof fc->keyword - 1);
    strncpy(fc->prefix, devkit_prefix ? devkit_prefix : "", sizeof fc->prefix - 1);
    strncpy(fc->desc, desc ? desc : "", sizeof fc->desc - 1);
    if (!add_to_game(fc))
        g_pending.push_back(fc);
    return 0;
}

// called after App::cAppSystem::Init and every frame until the manager exists
void cheats_flush_pending()
{
    for (size_t i = 0; i < g_pending.size();)
        if (add_to_game(g_pending[i]))
            g_pending.erase(g_pending.begin() + i);
        else
            break;
}

void cheats_register_builtin()
{
    cheat_register("sk", nullptr, "sk <devkit command> - run a SporeKit command, e.g. 'sk help', 'sk info'");
}

} // namespace sk
