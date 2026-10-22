#include "notify.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <mutex>
#include <vector>

#include "sui.h"

namespace notify {

static const SkHost* H;
static std::function<uint32_t()> g_font;
static std::mutex g_mu;
static std::deque<std::string> g_queue; // waiting to be built (outside the frame)
static bool g_build_queued, g_anim_queued;
static unsigned g_shown, g_failed;

constexpr float kW = 560, kH = 58, kGap = 8, kMargin = 24;
constexpr double kIn = 0.25, kOut = 0.35;
constexpr size_t kMaxVisible = 5;

struct Toast {
    sui::Win w = nullptr;
    std::string text;
    double t0 = 0, life = 6;
    float y = -1; // current top (slides when panels above go away)
};
static std::vector<Toast> g_toasts; // oldest first (top of the stack)
static sui::Win g_root;             // the main window the panels live in

static const char* kind_of(const std::string& s)
{
    auto has = [&](const char* w) { return s.find(w) != std::string::npos; };
    if (has("knocked out") || has("lost") || has("under attack") || has("failed"))
        return "danger";
    if (has("revived") || has("saved") || has("respawned") || has("evolved"))
        return "good";
    if (has(" left") || has("disconnected") || has("timeout") || has("closed"))
        return "leave";
    if (has("joined") || has("appeared") || has("connected") || has("hosting"))
        return "join";
    if (has("evolve") || has("ready") || has("vote"))
        return "vote";
    if (has(": "))
        return "chat";
    return "info";
}

static double life_of(const char* kind)
{
    std::string k = kind;
    return k == "danger" ? 10 : k == "vote" ? 10 : k == "chat" ? 8 : 6;
}

// The dialog font: about 9.3 px per glyph at 1080p; a label centres its text, so its box is made
// as wide as the text (coop_ui does the same). 46 glyphs fit a line; longer texts take two lines.
constexpr size_t kLine = 46;
constexpr uint32_t kDialogFont = 0x015A5D11; // the co-op window text font (logged by coop_ui)

static float text_w(const std::string& s) { return s.size() * 9.3f + 30.f; }

static std::vector<std::string> lines_of(const std::string& s)
{
    if (s.size() <= kLine)
        return {s};
    size_t cut = s.rfind(' ', kLine);
    if (cut == std::string::npos || cut < kLine / 2)
        cut = kLine;
    std::string a = s.substr(0, cut), b = s.substr(cut + (s[cut] == ' ' ? 1 : 0));
    if (b.size() > kLine)
        b = b.substr(0, kLine - 3) + "...";
    return {a, b};
}

static void screen(float& sw, float& sh)
{
    int w = 0, h = 0;
    H->screen_size(&w, &h);
    sw = w > 0 ? (float)w : 1440.f;
    sh = h > 0 ? (float)h : 1080.f;
}

static float top_of(size_t i, float sh) { return sh * 0.15f + i * (kH + kGap); }

static void remove(Toast& t)
{
    if (t.w && g_root)
        sui::remove_child(g_root, t.w);
    t.w = nullptr;
}

// outside the frame: build the queued panels
static void build(void*)
{
    std::deque<std::string> q;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        q.swap(g_queue);
        g_build_queued = false;
    }
    sui::Win root = sui::main_window();
    if (root != g_root) { // another main window (should not happen): forget the old panels
        g_toasts.clear();
        g_root = root;
    }
    if (!root)
        return;
    float sw, sh;
    screen(sw, sh);
    for (auto& text : q) {
        while (g_toasts.size() >= kMaxVisible) {
            remove(g_toasts.front());
            g_toasts.erase(g_toasts.begin());
        }
        const char* kind = kind_of(text);
        char name[32];
        snprintf(name, sizeof name, "toast_%s", kind);
        float y = top_of(g_toasts.size(), sh);
        sui::Win w = sui::image_window(root, sui::fnv(name), sui::fnv("sporecoop"), sw + 10, y);
        if (!w) {
            g_failed++;
            continue;
        }
        sui::set_flag(w, sui::kFlagIgnoreMouse, true);
        sui::set_flag(w, sui::kFlagAlwaysInFront, true);
        uint32_t font = g_font && g_font() ? g_font() : kDialogFont;
        auto lines = lines_of(text);
        for (size_t li = 0; li < lines.size(); li++) {
            float y0 = lines.size() == 1 ? 0.f : (li == 0 ? 5.f : 27.f);
            float h = lines.size() == 1 ? kH : 26.f;
            sui::Rect lr{68, y0, 68 + std::min(text_w(lines[li]), kW - 80), y0 + h};
            if (sui::Win l = sui::label(w, lr, lines[li], font, 0xFFFFFFFF, sui::kAlignLeft))
                sui::set_flag(l, sui::kFlagIgnoreMouse, true);
        }
        sui::bring_to_front(root, w);
        Toast t;
        t.w = w;
        t.text = text;
        t.t0 = H->time_seconds();
        t.life = life_of(kind);
        t.y = y;
        g_toasts.push_back(t);
        g_shown++;
    }
}

// outside the frame: move the panels
static void animate(void*)
{
    g_anim_queued = false;
    if (!g_root || g_toasts.empty())
        return;
    float sw, sh;
    screen(sw, sh);
    double now = H->time_seconds();
    float x_in = sw - kW - kMargin, x_out = sw + 10;
    for (size_t i = 0; i < g_toasts.size();) {
        Toast& t = g_toasts[i];
        double age = now - t.t0;
        if (age > t.life + kOut) {
            remove(t);
            g_toasts.erase(g_toasts.begin() + i);
            continue;
        }
        float k; // 0 = out of the screen, 1 = in place (ease out / ease in)
        if (age < kIn)
            k = 1.f - (float)std::pow(1.0 - age / kIn, 3);
        else if (age > t.life)
            k = 1.f - (float)std::pow((age - t.life) / kOut, 2);
        else
            k = 1.f;
        float want_y = top_of(i, sh);
        t.y += (want_y - t.y) * 0.25f; // close the gap of a panel that went away
        if (std::fabs(want_y - t.y) < 0.5f)
            t.y = want_y;
        float x = x_out + (x_in - x_out) * k;
        sui::set_area(t.w, {x, t.y, x + kW, t.y + kH});
        i++;
    }
}

void init(const SkHost* host, SkMod*, std::function<uint32_t()> font)
{
    H = host;
    g_font = std::move(font);
}

void show(const std::string& text)
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_queue.push_back(text);
    while (g_queue.size() > kMaxVisible)
        g_queue.pop_front();
    if (!g_build_queued) {
        g_build_queued = true;
        H->run_outside_frame(build, nullptr); // UI windows are never built inside Present
    }
}

void tick(double)
{
    if (!g_toasts.empty() && !g_anim_queued) {
        g_anim_queued = true;
        H->run_outside_frame(animate, nullptr);
    }
}

void shutdown()
{
    for (auto& t : g_toasts)
        remove(t);
    g_toasts.clear();
}

std::string status()
{
    char b[160];
    snprintf(b, sizeof b, "notify: shown=%u failed=%u visible=%u\n", g_shown, g_failed, (unsigned)g_toasts.size());
    std::string s = b;
    for (auto& t : g_toasts) {
        sui::Rect r = sui::area(t.w);
        snprintf(b, sizeof b, "toast %s (%.0f %.0f %.0f %.0f) '%s'\n", kind_of(t.text), r.x1, r.y1, r.x2, r.y2,
                 t.text.c_str());
        s += b;
    }
    return s;
}

} // namespace notify
