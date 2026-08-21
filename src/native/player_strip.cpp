// The control bar that floats over the picture while something is playing.
//
// mpv has every one of these on a keybinding, but expecting someone to know
// that '#' cycles audio and 'v' toggles subtitles is not an interface. This
// drives the same commands over mpv's IPC socket and shows the state.
//
// It lives in its own child window (see app_main) because mpv renders into a
// sibling child, and nothing drawn on the parent can appear above that. The
// window slides out of view when the pointer goes idle.

#include "../library.hpp"
#include "../player.hpp"
#include "../ui.hpp"
#include "view.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

// Rounded button. `primary` is the one action worth colouring.
bool button(Ui& u, int id, const Rect& r, const std::wstring& label, bool primary = false,
            bool quiet = false) {
    const bool clicked = u.clickable(id, r);
    const float h = u.hover(id);

    if (primary) {
        u.c.fill(r, h > 0.1f ? accentSoft : accent, r.h / 2);
    } else if (quiet) {
        u.c.fill(r, h > 0.1f ? gfx::rgb(0x2C2C3A) : gfx::rgb(0x1E1E28), 8);
    } else {
        u.c.fill(r, h > 0.1f ? gfx::rgb(0x2C2C3A) : gfx::rgb(0x20202B), 8);
        u.c.stroke(r, h > 0.1f ? accent.withAlpha(0.5f) : gfx::rgb(0x30303E), 8);
    }
    u.c.text(label, {r.x, r.y + (r.h - 17) / 2, r.w, 18},
             primary ? gfx::rgb(0x2A0D18) : (quiet ? dim : fg),
             f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));
    return clicked;
}

// A slider that reports the fraction it was set to, or -1 when untouched.
float slider(Ui& u, int id, const Rect& track, float value, Color fill) {
    u.c.fill(track, gfx::rgb(0x2A2A38), track.h / 2);
    u.c.fill({track.x, track.y, track.w * value, track.h}, fill, track.h / 2);

    const Rect hit{track.x - 4, track.y - 10, track.w + 8, track.h + 20};
    const bool over = hit.contains(u.in.mouseX, u.in.mouseY);
    u.clickable(id, hit);

    const float knob = over ? 6.5f : 5.0f;
    u.c.fill({track.x + track.w * value - knob, track.cy() - knob, knob * 2, knob * 2},
             over ? gfx::rgb(0xFFFFFF) : fill, knob);

    if (over && u.in.mouseDown) {
        return (std::max)(0.0f, (std::min)(1.0f, (u.in.mouseX - track.x) / track.w));
    }
    return -1.0f;
}

// Which track is selected, and how it should read on a button.
std::wstring trackLabel(const std::vector<player::Track>& tracks, const char* type,
                        const wchar_t* prefix) {
    int total = 0, ordinal = 0;
    const player::Track* current = nullptr;
    for (const auto& t : tracks) {
        if (t.type != type) continue;
        ++total;
        if (t.selected) {
            current = &t;
            ordinal = total;
        }
    }
    if (total == 0) return std::wstring(prefix) + L": none";
    if (!current) return std::wstring(prefix) + L": off";

    std::wstring name = widen(!current->lang.empty() ? current->lang : current->title);
    if (name.empty()) name = std::to_wstring(ordinal);

    wchar_t buf[96];
    if (total > 1) {
        swprintf(buf, 96, L"%s: %s (%d/%d)", prefix, name.c_str(), ordinal, total);
    } else {
        swprintf(buf, 96, L"%s: %s", prefix, name.c_str());
    }
    return buf;
}

// Advance to the next track of a type, wrapping. Subtitles get an extra "off"
// position, because turning them off is the single most wanted thing here.
void cycleTrack(const std::vector<player::Track>& tracks, const char* type, bool allowOff) {
    std::vector<int> ids;
    int currentAt = -1;
    for (const auto& t : tracks) {
        if (t.type != type) continue;
        if (t.selected) currentAt = static_cast<int>(ids.size());
        ids.push_back(t.id);
    }
    if (ids.empty()) return;
    if (allowOff) ids.push_back(0);  // 0 disables

    const int next = (currentAt + 1) % static_cast<int>(ids.size());
    if (std::string(type) == "audio") {
        player::setAudioTrack(ids[next]);
    } else {
        player::setSubTrack(ids[next]);
    }
}

}  // namespace

bool playerStrip(Ui& u, State& st) {
    const Rect full = u.c.bounds();
    const float w = full.w;
    constexpr float pad = 20;

    // Panel. The window's region rounds the corners; this just fills it and
    // lifts the top edge a shade so it reads as a surface over the video.
    u.c.fill(full, gfx::rgb(0x14141C));
    u.c.fill({0, 0, w, 1}, gfx::rgb(0x2E2E3C));

    const player::State ps = player::state();
    const ui::Status status = ui::status();

    // ---- still buffering: no scrubbing to offer yet ---------------------
    if (!ps.running || ps.duration <= 0) {
        u.c.text(widen(status.message), {pad, 20, w - pad * 2 - 110, 20}, fg,
                 f(13, gfx::Weight::Medium));
        const Rect bar{pad, 50, w - pad * 2 - 110, 5};
        u.c.fill(bar, gfx::rgb(0x2A2A38), 2.5f);
        u.c.fill({bar.x, bar.y, bar.w * (status.progress / 100.0f), bar.h}, accent, 2.5f);
        if (button(u, 4001, {w - pad - 90, 26, 90, 32}, L"Cancel")) ui::requestStop();
        return true;
    }

    // ---- seek -----------------------------------------------------------
    const float frac =
        ps.duration > 0 ? static_cast<float>((std::min)(1.0, ps.position / ps.duration)) : 0.0f;

    const Rect seek{pad + 52, 17, w - pad * 2 - 104, 5};
    const float dragged = slider(u, 4010, seek, frac, accent);
    if (dragged >= 0) player::seekAbsolute(dragged * ps.duration);

    u.c.text(formatTime(ps.position), {pad, 11, 46, 18}, dim,
             f(11.5f, gfx::Weight::Medium, gfx::Align::Right));
    u.c.text(formatTime(ps.duration), {w - pad - 46, 11, 46, 18}, dim,
             f(11.5f, gfx::Weight::Medium));

    // ---- right-hand cluster, laid out from the edge inwards -------------
    const float rowY = 40;
    const float rowH = 32;
    float right = w - pad;

    if (button(u, 4050, {right - 74, rowY, 74, rowH}, L"Stop", false, true)) {
        ui::requestStop();
    }
    right -= 74 + 12;

    const Rect volTrack{right - 84, rowY + rowH / 2 - 2.5f, 84, 5};
    const float vol = slider(u, 4040, volTrack, ps.volume / 100.0f, gfx::rgb(0x8A8A99));
    if (vol >= 0) player::setVolume(static_cast<int>(vol * 100));
    right -= 84 + 16;

    const std::wstring subs = trackLabel(ps.tracks, "sub", L"Subs");
    const float subsW = (std::max)(112.0f, subs.size() * 6.5f + 24);
    if (button(u, 4031, {right - subsW, rowY, subsW, rowH}, subs)) {
        cycleTrack(ps.tracks, "sub", true);
    }
    right -= subsW + 10;

    const std::wstring audio = trackLabel(ps.tracks, "audio", L"Audio");
    const float audioW = (std::max)(112.0f, audio.size() * 6.5f + 24);
    if (button(u, 4030, {right - audioW, rowY, audioW, rowH}, audio)) {
        cycleTrack(ps.tracks, "audio", false);
    }
    right -= audioW + 10;

    // ---- transport, from the left ---------------------------------------
    float x = pad;
    if (button(u, 4020, {x, rowY, 82, rowH}, ps.paused ? L"Play" : L"Pause", true)) {
        player::togglePause();
    }
    x += 82 + 10;
    if (button(u, 4021, {x, rowY, 56, rowH}, L"-10s")) player::seekRelative(-10);
    x += 56 + 8;
    if (button(u, 4022, {x, rowY, 56, rowH}, L"+30s")) player::seekRelative(30);
    x += 56 + 18;

    // ---- what is playing, in whatever room is left ----------------------
    if (right - x > 90) {
        std::wstring what = widen(st.opened.title.empty() ? st.opened.torrentName
                                                          : st.opened.title);
        if (st.resumeEpisode > 0) {
            what += L"  -  Episode " + std::to_wstring(st.resumeEpisode);
        }
        u.c.text(what, {x, rowY + 7, right - x - 12, 20}, dim, f(12));
    }

    // The playhead moves on its own, so this is never idle.
    return true;
}

}  // namespace tsuzuki::view
