// The screen you watch while an episode gets ready.
//
// Fetching metadata and filling the buffer both take real time, and a page
// that just sits there gives no way to tell a slow swarm from a stuck one. So
// this shows what is actually happening: which phase, how far through, how
// many peers answered, and how fast data is arriving.
//
// The peers are drawn as dots rather than only counted. A number climbing from
// 3 to 40 is information; forty dots filling in is the same information read at
// a glance, and it makes an unavoidable wait feel like progress.

#include "../ui.hpp"
#include "view.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

constexpr float kPanelW = 470;
constexpr float kPanelH = 236;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

std::wstring rateText(long long bytesPerSecond) {
    if (bytesPerSecond <= 0) return L"-";
    const double mb = static_cast<double>(bytesPerSecond) / (1024.0 * 1024.0);
    wchar_t buf[32];
    if (mb >= 1.0) {
        swprintf(buf, 32, L"%.1f MB/s", mb);
    } else {
        swprintf(buf, 32, L"%.0f KB/s", bytesPerSecond / 1024.0);
    }
    return buf;
}

// A light sweep travelling along the bar. Cheap, and it separates "slowly
// filling" from "stopped" at a glance.
void shimmer(Ui& u, const Rect& bar, float radius) {
    const float period = 1500.0f;
    const float t = static_cast<float>(GetTickCount() % static_cast<DWORD>(period)) / period;
    const float bandW = bar.w * 0.28f;
    const float x = bar.x - bandW + (bar.w + bandW * 2) * t;

    const Rect band{x, bar.y, bandW, bar.h};
    // Clipped to the bar so the sweep cannot run past either end.
    u.c.pushClip(bar);
    u.c.gradient(band, gfx::rgb(0xFFFFFF, 0.0f), gfx::rgb(0xFFFFFF, 0.16f), radius);
    u.c.popClip();
}

}  // namespace

// Returns true while it should keep repainting.
bool waitingPanel(Ui& u, State& st, const std::wstring& heading) {
    const ui::Status s = ui::status();
    const Rect full = u.c.bounds();

    const Rect panel{full.cx() - kPanelW / 2, full.cy() - kPanelH / 2 - 20, kPanelW, kPanelH};
    u.c.fill(panel, card, 14);
    u.c.stroke(panel, line, 14);

    float y = panel.y + 22;
    u.c.text(heading, {panel.x + 24, y, panel.w - 48, 26}, fg,
             f(16.5f, gfx::Weight::Semibold, gfx::Align::Center));
    y += 32;

    // What the engine is doing right now, in its own words.
    const std::wstring msg = widen(s.message);
    u.c.text(msg.empty() ? L"Starting..." : msg, {panel.x + 24, y, panel.w - 48, 20}, dim,
             f(12.5f, gfx::Weight::Regular, gfx::Align::Center));
    y += 34;

    // ---- progress -------------------------------------------------------
    const Rect bar{panel.x + 24, y, panel.w - 48, 8};
    u.c.fill(bar, gfx::rgb(0x22222E), 4);
    const float frac = (std::max)(0.0f, (std::min)(1.0f, s.progress / 100.0f));
    if (frac > 0.001f) {
        u.c.fill({bar.x, bar.y, bar.w * frac, bar.h}, accent, 4);
    }
    shimmer(u, bar, 4);
    y += 22;

    wchar_t pct[32];
    swprintf(pct, 32, L"%d%%", s.progress);
    u.c.text(pct, {panel.x + 24, y, panel.w - 48, 18}, dim,
             f(11.5f, gfx::Weight::Medium, gfx::Align::Center));
    y += 30;

    // ---- the swarm ------------------------------------------------------
    {
        constexpr int kMaxDots = 28;
        const int shown = (std::min)(s.peers, kMaxDots);
        const float dot = 6.0f, gap = 5.0f;
        const float rowW = kMaxDots * dot + (kMaxDots - 1) * gap;
        float x = panel.cx() - rowW / 2;

        for (int i = 0; i < kMaxDots; ++i) {
            const bool on = i < shown;
            // The leading few pulse, so a swarm that is still growing looks
            // like it is still growing.
            float alpha = on ? 1.0f : 0.12f;
            if (on && i >= shown - 3) {
                const float t = static_cast<float>(GetTickCount() % 1200) / 1200.0f;
                alpha = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(t * 6.2831853f + i));
            }
            u.c.fill({x, y, dot, dot}, on ? accent.withAlpha(alpha) : gfx::rgb(0x2A2A38),
                     dot / 2);
            x += dot + gap;
        }
        y += 18;
    }

    wchar_t swarm[128];
    if (s.peers > 0) {
        swprintf(swarm, 128, L"%d peers  -  %d seeding  -  %s", s.peers, s.seeds,
                 rateText(s.downloadRate).c_str());
    } else {
        swprintf(swarm, 128, L"looking for peers...");
    }
    u.c.text(swarm, {panel.x + 24, y, panel.w - 48, 18}, dim,
             f(11.5f, gfx::Weight::Regular, gfx::Align::Center));
    y += 30;

    // ---- give up --------------------------------------------------------
    const Rect cancel{panel.cx() - 60, panel.bottom() - 46, 120, 32};
    const bool hot = u.clickable(6001, cancel);
    u.c.fill(cancel, u.hover(6001) > 0.1f ? gfx::rgb(0x2C2C3A) : gfx::rgb(0x20202B), 8);
    u.c.stroke(cancel, line, 8);
    u.c.text(L"Cancel", {cancel.x, cancel.y + 7, cancel.w, 18}, fg,
             f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));
    if (hot) ui::requestStop();

    u.contentHeight = 0;
    return true;  // the swarm and the sweep both move
}

}  // namespace tsuzuki::view
