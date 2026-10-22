#include "coop_ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>

#include "modes.h"
#include "sui.h"

namespace coop_ui {

static uint32_t g_text_font;

using sui::Rect;
using sui::Win;

static const SkHost* H;
static SkMod* M;
static Api A;
static bool g_ok;      // Spore UI available
static bool g_ru;      // Russian captions
static bool g_want_open, g_pending, g_dirty = true;
static double g_last_refresh;
static std::string g_status;
static uint32_t g_status_color = 0xFFFFD866;
static bool g_f9_down;
static bool g_raise = true; // move the window above all UI layers when it opens
bool g_log_msgs; // debug: log UI messages that reach our handlers

constexpr uint32_t kIdMenuButton = 0xC00F0001, kIdDialog = 0xC00F0100;
constexpr uint32_t kIdMainMenu = 0x0615A5FF, kIdShare = 0x0615B616;
constexpr uint32_t kDialogLayout = 0x34A6339E; // the game's account dialog (frame, logo, close)
constexpr uint32_t kGold = 0xFFFFCC4D, kWhite = 0xFFFFFFFF, kDim = 0xFF9DB8CC, kGreen = 0xFF8CF08C,
                   kRed = 0xFFFF7A6A, kYellow = 0xFFFFD866;
constexpr float kW = 760, kH = 560;

static const char* T(const char* en, const char* ru) { return g_ru ? ru : en; }

// ---------------- widgets with cached state ----------------
struct Cache {
    std::string text;
    uint32_t color = 0;
    int vis = -1;
    bool left = false; // left aligned label: the box is sized to the text (labels center their text)
    Rect box;
};
static std::map<Win, Cache> g_cache;

// width of a text in the dialog font (average glyph width, measured at 1080p)
static float text_width(const std::string& t)
{
    int n = 0;
    for (unsigned char ch : t)
        n += (ch & 0xC0) != 0x80;
    return n * 9.3f + 30;
}

static void fit(Win w, const Cache& c)
{
    Rect r = c.box;
    r.x2 = std::min(r.x2, r.x1 + text_width(c.text));
    sui::set_area(w, r);
}

static void text(Win w, const std::string& t)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.text != t) {
        c.text = t;
        sui::set_caption(w, t);
        if (c.left)
            fit(w, c);
    }
}
static void color(Win w, uint32_t argb)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.color != argb) {
        c.color = argb;
        sui::set_label_color(w, argb);
    }
}
static void show(Win w, bool on)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.vis != (int)on) {
        c.vis = on;
        sui::set_visible(w, on);
    }
}

// ---------------- main menu item ----------------
static Win g_menu_btn, g_menu_parent;
static void* g_menu_drawable;

static void on_menu_click() { open(true); }

static void ensure_menu_button()
{
    Win root = sui::main_window();
    Win menu = A.mode && A.mode() == modes::kMenu ? sui::find(root, kIdMainMenu) : nullptr;
    if (!menu) {
        g_menu_btn = g_menu_parent = nullptr;
        return;
    }
    if (g_menu_btn && g_menu_parent == menu)
        return;
    Win share = sui::find(menu, kIdShare);
    if (!share)
        return;
    // a stale item from before a hot reload: remove it, a fresh one gets our handler
    if (Win old = sui::find(menu, kIdMenuButton))
        sui::remove_child(menu, old);
    Rect a = sui::area(share);
    float dy = 95; // below "Galactic Adventures"
    Rect r{a.x1, a.y1 + dy, a.x2, a.y2 + dy};
    Win b = sui::button_like(menu, share, r, T("Co-op", "Кооператив"));
    if (!b)
        return;
    sui::set_ids(b, kIdMenuButton, kIdMenuButton);
    if (!g_menu_drawable) {
        void* icon = sui::image(sui::fnv("coop_icon"), 0x2F7D0004, sui::fnv("sporecoop"));
        g_menu_drawable = sui::std_drawable_with_icon(sui::drawable(share), icon);
        if (!icon)
            H->log(M, SK_WARN, "ui: coop_icon not found (Data/SporeCoop.package missing?)");
    }
    if (g_menu_drawable)
        sui::set_drawable(b, g_menu_drawable);
    sui::add_handler(b, [](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg 0x%X src=%p %08X %08X %08X %08X", m.type, m.source, m.a, m.b, m.c, m.d);
        if (sui::is_click(m)) {
            on_menu_click();
            return true;
        }
        return false;
    });
    g_menu_btn = b;
    g_menu_parent = menu;
    H->log(M, SK_INFO, "ui: co-op item added to the main menu");
}

// ---------------- dialog ----------------
struct Dialog {
    Win root = nullptr, layer = nullptr;
    uint32_t font_body = 0, font_button = 0;
    Win like_button = nullptr; // gold "Login" button of the layout
    Win subtitle = nullptr;
    // connect page
    std::vector<Win> page_connect;
    Win name_edit = nullptr, addr_edit = nullptr, port_edit = nullptr, join_btn = nullptr, host_btn = nullptr;
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr;
    Win lan_rows[3] = {};
    std::string lan_addr[3];
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr;
    Win row_name[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win chat_lines[5] = {};
    Win creation = nullptr, creation_prev = nullptr, creation_next = nullptr;
};
static Dialog D;
static std::map<std::string, Win> g_named; // for tests (coop.uistate)

static Win add_label(std::vector<Win>& page, const Rect& r, const std::string& t, uint32_t argb,
                     int align = sui::kAlignLeft)
{
    Win w = sui::label(D.root, r, t, D.font_body, argb, align);
    if (!w)
        return nullptr;
    page.push_back(w);
    Cache c{t, argb, 1, align == sui::kAlignLeft, r};
    if (c.left)
        fit(w, c);
    g_cache[w] = c;
    return w;
}

static Win add_button(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t,
                      std::function<void()> fn)
{
    Win w = sui::button_like(D.root, D.like_button, r, t);
    if (!w)
        return nullptr;
    sui::set_font(w, D.font_body); // the dialog text font reads better than the small button font
    page.push_back(w);
    g_named[name] = w;
    g_cache[w] = Cache{t, 0, 1};
    sui::add_handler(w, [fn, name = std::string(name)](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg %s 0x%X src=%p %08X %08X %08X", name.c_str(), m.type, m.source, m.a, m.b, m.c);
        if (sui::is_click(m)) {
            fn();
            return true;
        }
        return false;
    });
    return w;
}

static Win add_edit(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t, int max_len)
{
    Win w = sui::text_edit(D.root, r, t, D.font_body, max_len);
    if (w) {
        page.push_back(w);
        g_named[name] = w;
    }
    return w;
}

static void set_status(const std::string& s, uint32_t c)
{
    g_status = s;
    g_status_color = c;
    g_dirty = true;
}

static void save_str(const char* section, const char* key, const std::string& v)
{
    if (H->config_set_str)
        H->config_set_str(M, section, key, v.c_str());
}
static std::string load_str(const char* section, const char* key, const char* def)
{
    char b[256];
    H->config_str(M, section, key, def, b, sizeof b);
    return b;
}

static void apply_name()
{
    std::string n = sui::edit_text(D.name_edit);
    if (!n.empty() && n != A.name()) {
        A.set_name(n);
        save_str("coop", "name", n);
    }
}

static void do_join(const std::string& address)
{
    apply_name();
    std::string addr = address;
    while (!addr.empty() && addr.back() == ' ')
        addr.pop_back();
    if (addr.empty()) {
        set_status(T("Enter the host address, e.g. 192.168.1.5:47810", "Введите адрес хоста, например 192.168.1.5:47810"),
                   kRed);
        return;
    }
    std::string err;
    if (!A.join(addr, err)) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed);
        return;
    }
    save_str("ui", "last_address", addr);
    set_status(std::string(T("Connecting to ", "Подключение к ")) + addr + "...", kYellow);
}

static void do_host()
{
    apply_name();
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    std::string err;
    if (!A.host((uint16_t)port, err)) {
        set_status(std::string(T("Cannot host: ", "Не удалось создать игру: ")) + err, kRed);
        return;
    }
    save_str("ui", "host_port", std::to_string(port));
    set_status("", kYellow);
}

static void do_send_chat()
{
    std::string t = sui::edit_text(D.chat_edit);
    if (t.empty())
        return;
    A.say(t);
    sui::set_edit_text(D.chat_edit, "");
    g_dirty = true;
}

static void build_dialog()
{
    Win root = sui::find(sui::main_window(), kIdDialog);
    if (root) { // left over from before a hot reload: its handlers are dead
        if (Win p = sui::parent(root))
            sui::remove_child(p, root);
    }
    root = sui::load_layout(kDialogLayout);
    if (!root) {
        H->log(M, SK_ERROR, "ui: cannot load the dialog layout %08X", kDialogLayout);
        return;
    }
    D = Dialog{};
    g_named.clear();
    D.root = root;
    D.layer = sui::parent(root);
    sui::add_ref(root); // ours: the window survives mode changes that clear the layer
    sui::set_ids(root, kIdDialog, 0);
    Win text_win = sui::find(root, 0x0451D381), login = sui::find(root, 0x0519E168);
    D.font_body = sui::font(text_win);
    g_text_font = D.font_body;
    H->log(M, SK_DEBUG, "ui: dialog font %08X", D.font_body);
    D.like_button = login;
    D.font_button = sui::font(login);
    // keep the frame, glow, logo and close button; hide the account dialog's own content
    for (uint32_t id : {0x0451D381u, 0x0519E168u, 0x06555818u, 0x065D20A8u, 0x0519D9B8u, 0x0451A4C0u})
        if (Win w = sui::find(root, id))
            sui::set_visible(w, false);
    if (Win x = sui::find(root, 0xFFFFFFF2u)) {
        g_named["close"] = x;
        sui::add_handler(x, [](Win, const sui::Msg& m) {
            if (sui::is_click(m)) {
                open(false);
                return true;
            }
            return false;
        });
    }
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    Rect a{(sw - kW) / 2, (sh - kH) / 2, (sw + kW) / 2, (sh + kH) / 2};
    sui::set_area(root, a);

    std::vector<Win> common;
    D.subtitle = add_label(common, {0, 70, kW, 96}, T("CO-OP", "КООПЕРАТИВ"), kGold, sui::kAlignCenter);

    // ---- connect page ----
    auto& P = D.page_connect;
    add_label(P, {40, 112, 225, 140}, T("Your name", "Ваше имя"), kWhite);
    D.name_edit = add_edit(P, "name", {230, 108, 520, 140}, A.name(), 24);
    add_label(P, {40, 158, 720, 186}, T("Join a friend's game", "Подключиться к другу"), kGold);
    add_label(P, {40, 198, 225, 226}, T("Host address", "Адрес хоста"), kWhite);
    D.addr_edit = add_edit(P, "address", {230, 194, 520, 226}, load_str("ui", "last_address", ""), 64);
    D.join_btn = add_button(P, "join", {540, 190, 720, 230}, T("Join", "Подключиться"),
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    D.lan_hdr = add_label(P, {40, 242, 720, 268}, "", kDim);
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "lan%d", i);
        D.lan_rows[i] = add_button(P, nm, {40, 272.f + i * 38, 720, 306.f + i * 38}, "", [i] {
            if (!D.lan_addr[i].empty()) {
                sui::set_edit_text(D.addr_edit, D.lan_addr[i]);
                do_join(D.lan_addr[i]);
            }
        });
    }
    add_label(P, {40, 390, 720, 418}, T("Host a game", "Создать игру"), kGold);
    add_label(P, {40, 430, 225, 458}, T("Port (UDP)", "Порт (UDP)"), kWhite);
    D.port_edit = add_edit(P, "port", {230, 426, 330, 458},
                           load_str("ui", "host_port", std::to_string(A.default_port).c_str()), 5);
    D.host_btn = add_button(P, "host", {540, 422, 720, 462}, T("Host", "Создать"), do_host);
    D.host_hint = add_label(P, {40, 466, 720, 492}, "", kDim);
    D.status = add_label(P, {40, 498, 720, 526}, "", kYellow);

    // ---- lobby page ----
    auto& L = D.page_lobby;
    D.session = add_label(L, {40, 104, 720, 130}, "", kWhite);
    add_label(L, {40, 138, 300, 162}, T("Player", "Игрок"), kDim);
    add_label(L, {300, 138, 540, 162}, T("Stage", "Этап"), kDim);
    add_label(L, {540, 138, 620, 162}, T("Ping", "Пинг"), kDim);
    add_label(L, {620, 138, 720, 162}, T("Ready", "Готов"), kDim);
    for (int i = 0; i < 4; i++) {
        float y = 166.f + i * 28;
        D.row_name[i] = add_label(L, {40, y, 300, y + 26}, "", kWhite);
        D.row_stage[i] = add_label(L, {300, y, 540, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {540, y, 620, y + 26}, "", kWhite);
        D.row_ready[i] = add_label(L, {620, y, 720, y + 26}, "", kWhite);
    }
    D.world = add_label(L, {40, 284, 720, 310}, "", kYellow);
    for (int i = 0; i < 4; i++)
        D.chat_lines[i] = add_label(L, {40, 316.f + i * 23, 720, 338.f + i * 23}, "", kWhite);
    // the own creation of this stage: "My creature: Kirgle  < >"
    D.creation = add_label(L, {40, 408, 560, 434}, "", kGold);
    D.creation_prev = add_button(L, "cprev", {570, 404, 640, 434}, "<", [] {
        if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.creation_next = add_button(L, "cnext", {650, 404, 720, 434}, ">", [] {
        if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    D.chat_edit = add_edit(L, "chatedit", {40, 438, 560, 470}, "", 120);
    D.send_btn = add_button(L, "send", {570, 434, 720, 474}, T("Send", "Отправить"), do_send_chat);
    D.ready_btn = add_button(L, "ready", {40, 488, 240, 528}, "", [] {
        A.set_ready(!A.ready());
        g_dirty = true;
    });
    D.share_btn = add_button(L, "share", {260, 488, 480, 528}, T("Send my world", "Отправить мир"), [] {
        A.share_world();
        set_status(T("Sending the world to everyone...", "Мир отправляется всем..."), kYellow);
    });
    D.leave_btn = add_button(L, "leave", {500, 488, 720, 528}, "", [] {
        A.leave();
        set_status(T("Left the session", "Вы вышли из игры"), kYellow);
    });
    // Enter in the chat field sends the message
    if (D.chat_edit)
        sui::add_handler(D.chat_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_send_chat();
                return true;
            }
            return false;
        });
    if (D.addr_edit)
        sui::add_handler(D.addr_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_join(sui::edit_text(D.addr_edit));
                return true;
            }
            return false;
        });
    for (Win w : common)
        (void)w;
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    bool scanning = A.role() == 0;
    auto games = A.lan_games ? A.lan_games() : std::vector<LanGame>{};
    text(D.lan_hdr, games.empty() ? T("Games on your network: searching...", "Игры в вашей сети: поиск...")
                                  : T("Games on your network (click to join):", "Игры в вашей сети (нажмите, чтобы войти):"));
    for (int i = 0; i < 3; i++) {
        bool have = i < (int)games.size();
        if (have) {
            const auto& g = games[i];
            char b[256];
            snprintf(b, sizeof b, "%s  -  %s  -  %d/%d  -  %s", g.name.c_str(), g.info.c_str(), g.players,
                     g.max_players, g.address.c_str());
            text(D.lan_rows[i], b);
            D.lan_addr[i] = g.address;
        } else
            D.lan_addr[i].clear();
        show(D.lan_rows[i], have && scanning);
    }
    std::string hint;
    if (A.local_addresses) {
        auto ips = A.local_addresses();
        std::string port = sui::edit_text(D.port_edit);
        for (size_t i = 0; i < ips.size() && i < 3; i++)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    text(D.host_hint, hint.empty() ? "" : std::string(T("Friends connect to: ", "Друзья подключаются к: ")) + hint);
    text(D.status, g_status);
    color(D.status, g_status_color);
}

static void refresh_lobby()
{
    text(D.session, A.session_line());
    auto rows = A.players();
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        show(D.row_name[i], have);
        show(D.row_stage[i], have);
        show(D.row_ping[i], have);
        show(D.row_ready[i], have);
        if (!have)
            continue;
        const auto& p = rows[i];
        std::string n = p.name;
        if (p.host)
            n += T("  (host)", "  (хост)");
        if (p.me)
            n += T("  - you", "  - вы");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : kWhite);
        text(D.row_stage[i], p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(p.ping_ms) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? T("ready", "готов") : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    std::string wl = A.world_line ? A.world_line() : "";
    text(D.world, wl.empty() ? g_status : wl);
    auto chat = A.chat ? A.chat() : std::vector<std::string>{};
    size_t first = chat.size() > 4 ? chat.size() - 4 : 0;
    for (int i = 0; i < 4; i++)
        text(D.chat_lines[i], first + i < chat.size() ? chat[first + i] : "");
    int ck = 0, cown = 0;
    std::string cname, cl;
    if (A.creation && A.creation(ck, cname, cown)) {
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        cl += cname.empty() ? T("the world's own", "из мира хоста") : cname;
        if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
    }
    text(D.creation, cl);
    bool can = !cl.empty() && ck != 2 && cown > 0;
    show(D.creation_prev, can);
    show(D.creation_next, can);
    bool host = A.role() == 1;
    text(D.ready_btn, A.ready() ? T("Not ready", "Не готов") : T("I'm ready", "Я готов"));
    show(D.share_btn, host);
    text(D.share_btn, A.vote_active && A.vote_active() ? T("Start now", "Начать сейчас") : T("Send my world", "Отправить мир"));
    text(D.leave_btn, host ? T("Stop hosting", "Закрыть игру") : T("Leave", "Выйти"));
}

static void refresh()
{
    g_pending = false;
    if (!g_ok)
        return;
    ensure_menu_button();
    if (g_want_open && !D.root)
        build_dialog();
    if (!D.root)
        return;
    if (g_want_open && g_raise) {
        // on top of every UI layer (in game the HUD layers cover the menu layer and take
        // the mouse): a direct child of the main window, brought to the front
        g_raise = false;
        Win main = sui::main_window();
        if (Win p = sui::parent(D.root))
            if (p != main)
                sui::remove_child(p, D.root);
        if (sui::parent(D.root) != main)
            sui::add_child(main, D.root);
        sui::bring_to_front(main, D.root);
        sui::set_flag(D.root, sui::kFlagAlwaysInFront, true);
    }
    show(D.root, g_want_open);
    if (!g_want_open) {
        if (A.lan_scan)
            A.lan_scan(false);
        return;
    }
    bool lobby = A.role() != 0;
    for (Win w : D.page_connect)
        show(w, !lobby);
    for (Win w : D.page_lobby)
        show(w, lobby);
    text(D.subtitle, lobby ? T("CO-OP LOBBY", "ЛОББИ КООПЕРАТИВА") : T("CO-OP", "КООПЕРАТИВ"));
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby)
        refresh_lobby();
    else
        refresh_connect();
}

static void refresh_task(void*) { refresh(); }

void init(const SkHost* host, SkMod* mod, Api api)
{
    H = host;
    M = mod;
    A = std::move(api);
    g_ok = sui::init(H, M);
    std::string lang = load_str("ui", "lang", "auto");
    g_ru = lang == "ru" || (lang == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    if (g_ok)
        sui::register_commands();
}

void open(bool on)
{
    if (g_want_open == on)
        return;
    g_want_open = on;
    if (on) {
        g_raise = true;
        if (D.root)
            sui::set_edit_text(D.name_edit, A.name()); // fresh values when the dialog appears
    }
    g_dirty = true;
}

bool is_open() { return g_want_open; }

void tick()
{
    if (!g_ok)
        return;
    // F9: show/hide the co-op window anywhere in the game
    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !g_f9_down)
        open(!g_want_open);
    g_f9_down = f9;
    double now = H->time_seconds();
    double period = g_want_open ? 0.25 : 1.0;
    if (!g_pending && (g_dirty || now - g_last_refresh >= period)) {
        g_pending = true;
        g_dirty = false;
        g_last_refresh = now;
        H->run_outside_frame(refresh_task, nullptr);
    }
}

void shutdown()
{
    if (D.root)
        sui::set_visible(D.root, false);
    if (g_menu_btn)
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    std::string s = "open=" + std::to_string(g_want_open) + " page=" + (A.role() ? "lobby" : "connect") +
                    " menu_button=" + (g_menu_btn ? "yes" : "no") + "\n";
    char b[256];
    if (g_menu_btn) {
        Rect r = sui::screen_rect(g_menu_btn);
        snprintf(b, sizeof b, "menu %.0f %.0f %.0f %.0f %d '%s'\n", r.x1, r.y1, r.x2, r.y2,
                 (int)sui::visible(g_menu_btn), sui::caption(g_menu_btn).c_str());
        s += b;
    }
    for (auto& [name, w] : g_named) {
        Rect r = sui::screen_rect(w);
        bool vis = sui::visible(w) && D.root && sui::visible(D.root);
        std::string cap = name == "name" || name == "address" || name == "port" || name == "chatedit"
                              ? sui::edit_text(w) : sui::caption(w);
        snprintf(b, sizeof b, "%s %.0f %.0f %.0f %.0f %d '%s'\n", name.c_str(), r.x1, r.y1, r.x2, r.y2, (int)vis,
                 cap.c_str());
        s += b;
    }
    for (int i = 0; i < 4; i++)
        if (D.row_name[i] && sui::visible(D.row_name[i]))
            s += "row " + std::to_string(i) + " '" + sui::caption(D.row_name[i]) + "' '" + sui::caption(D.row_stage[i]) +
                 "' '" + sui::caption(D.row_ping[i]) + "' '" + sui::caption(D.row_ready[i]) + "'\n";
    for (int i = 0; i < 5; i++)
        if (D.chat_lines[i] && !sui::caption(D.chat_lines[i]).empty())
            s += "chat '" + sui::caption(D.chat_lines[i]) + "'\n";
    if (D.session)
        s += "session '" + sui::caption(D.session) + "'\n";
    if (D.world)
        s += "world '" + sui::caption(D.world) + "'\n";
    if (D.creation)
        s += "creation '" + sui::caption(D.creation) + "'\n";
    if (D.lan_hdr)
        s += "lan '" + sui::caption(D.lan_hdr) + "'\n";
    s += "status '" + g_status + "'\n";
    return s;
}

uint32_t text_font() { return g_text_font; }

} // namespace coop_ui
