// The settings screen, and the handful of controls it needs.
//
// Kept apart from view.cpp because it is mostly a long list of rows, and
// because the controls here - toggle, text field, segmented picker - are the
// only place the interface needs real input handling.

#include "../track.hpp"
#include "../ui.hpp"
#include "view.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

constexpr float kPad = 26;
constexpr float kHeaderH = 62;
constexpr float kRowH = 46;
constexpr float kCtrlW = 300;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

// ---------------------------------------------------------------- controls

// A pill that slides. Returns true if it was just changed.
bool toggle(Ui& u, int id, const Rect& r, bool& value) {
    const Rect pill{r.right() - 46, r.y + (r.h - 24) / 2, 46, 24};
    const bool clicked = u.clickable(id, pill);
    if (clicked) value = !value;

    u.c.fill(pill, value ? accent : gfx::rgb(0x2A2A38), 12);
    const float knobX = value ? pill.right() - 21 : pill.x + 3;
    u.c.fill({knobX, pill.y + 3, 18, 18}, value ? gfx::rgb(0x2A0D18) : gfx::rgb(0x8A8A99), 9);
    return clicked;
}

// One row of mutually exclusive choices. Returns the index now selected.
int segmented(Ui& u, int id, const Rect& r, const wchar_t* const* labels, const int count,
              int current) {
    const float segW = r.w / count;
    u.c.fill(r, gfx::rgb(0x16161F), 8);
    u.c.stroke(r, line, 8);

    for (int i = 0; i < count; ++i) {
        const Rect seg{r.x + i * segW, r.y, segW, r.h};
        const bool on = i == current;
        if (u.clickable(id + i, seg)) current = i;
        if (on) u.c.fill(seg.inset(3), accent, 6);
        u.c.text(labels[i], {seg.x, seg.y + (seg.h - 17) / 2, seg.w, 18},
                 on ? gfx::rgb(0x2A0D18) : dim,
                 f(12.5f, on ? gfx::Weight::Semibold : gfx::Weight::Regular, gfx::Align::Center));
    }
    return current;
}

// Editable text. Focus lives in State so exactly one field has it at a time.
void field(Ui& u, State& st, int id, const Rect& r, std::wstring& value,
           const wchar_t* placeholder) {
    const bool over = r.contains(u.in.mouseX, u.in.mouseY);
    if (u.in.mousePressed && over) st.focusField = id;
    else if (u.in.mousePressed && !over && st.focusField == id) st.focusField = 0;

    const bool focused = st.focusField == id;
    u.c.fill(r, gfx::rgb(0x16161F), 8);
    u.c.stroke(r, focused ? accent : line, 8);

    const Rect inner{r.x + 11, r.y + (r.h - 19) / 2, r.w - 22, 19};
    if (value.empty() && !focused) {
        u.c.text(placeholder, inner, dim, f(12.5f));
    } else {
        u.c.text(value, inner, fg, f(12.5f));
        if (focused && (GetTickCount() / 500) % 2 == 0) {
            const float cw = value.size() * 6.3f;
            u.c.fill({inner.x + cw + 1, inner.y + 1, 1.4f, 16}, accent);
        }
    }

    if (focused && !u.in.typed.empty()) {
        for (const wchar_t ch : u.in.typed) {
            if (ch == L'\b') {
                if (!value.empty()) value.pop_back();
            } else if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
                st.focusField = 0;
            } else if (ch >= 32) {
                value.push_back(ch);
            }
        }
        st.draftDirty = true;
        st.draftAt = GetTickCount();
    }
}

// ------------------------------------------------------------------ layout

// Label on the left, control on the right, with the explanation underneath.
Rect row(Ui& u, float& y, float w, const wchar_t* label, const wchar_t* note) {
    const float h = note ? kRowH + 14 : kRowH;
    u.c.text(label, {kPad, y + 12, w - kCtrlW - 40, 20}, fg, f(13.5f, gfx::Weight::Medium));
    if (note) {
        u.c.text(note, {kPad, y + 31, w - kCtrlW - 40, 18}, dim, f(11.5f));
    }
    const Rect ctrl{w - kPad - kCtrlW, y + (h - 32) / 2, kCtrlW, 32};
    y += h;
    return ctrl;
}

void groupHeader(Ui& u, float& y, const wchar_t* title) {
    y += 14;
    u.c.text(title, {kPad, y, 400, 22}, accent, f(12.5f, gfx::Weight::Bold));
    y += 26;
    u.c.fill({kPad, y - 6, u.c.bounds().w - kPad * 2, 1}, line);
}

std::wstring numText(double v, bool integer) {
    wchar_t buf[32];
    if (integer) {
        swprintf(buf, 32, L"%d", static_cast<int>(v));
    } else {
        swprintf(buf, 32, L"%g", v);
    }
    return buf;
}

int indexOf(const std::string& value, const char* const* options, int count, int fallback) {
    for (int i = 0; i < count; ++i) {
        if (value == options[i]) return i;
    }
    return fallback;
}

}  // namespace

bool settingsScreen(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    bool wantMore = false;

    if (!st.draftLoaded) {
        st.draft = ui::settings();
        st.draftLoaded = true;
        st.draftDirty = false;
    }
    Settings& s = st.draft;
    const Settings before = s;

    float y = kHeaderH + 22 - st.scroll[static_cast<int>(Screen::Settings)];

    u.c.text(L"Settings", {kPad, y, 400, 30}, fg, f(21, gfx::Weight::Bold));
    y += 40;

    // ---- Account ------------------------------------------------------
    groupHeader(u, y, L"ACCOUNT");
    {
        const track::Account acc = track::account();
        const Rect ctrl = row(u, y, w,
                              acc.linked ? L"AniList" : L"AniList - not linked",
                              acc.linked ? L"Progress syncs automatically as you watch."
                                         : L"Link to sync watch progress and see your lists.");
        const Rect btn{ctrl.right() - 150, ctrl.y, 150, 32};
        const bool clicked = u.clickable(7100, btn);
        u.c.fill(btn, acc.linked ? gfx::rgb(0x22222E) : accent, 8);
        u.c.text(acc.linked ? L"Unlink" : L"Link AniList", {btn.x, btn.y + 8, btn.w, 18},
                 acc.linked ? fg : gfx::rgb(0x2A0D18),
                 f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));
        if (clicked) {
            if (acc.linked) {
                ui::logoutAniList();
            } else {
                std::string err;
                if (!ui::startAniListLogin(err)) st.message = widen(err);
            }
        }
        if (acc.linked && !acc.name.empty()) {
            u.c.text(L"Signed in as " + widen(acc.name), {kPad, y, w - kPad * 2, 18}, dim, f(11.5f));
            y += 22;
        }
    }
    {
        const Rect ctrl = row(u, y, w, L"Sync progress to AniList",
                              L"Marks an episode watched once you reach the end.");
        toggle(u, 7110, ctrl, s.syncProgress);
    }

    // ---- Playback -----------------------------------------------------
    groupHeader(u, y, L"PLAYBACK");
    {
        static const wchar_t* quality[] = {L"1080p", L"720p", L"480p"};
        static const char* qualityValues[] = {"1080", "720", "480"};
        const Rect ctrl = row(u, y, w, L"Preferred quality", nullptr);
        const int pick = segmented(u, 7200, ctrl, quality, 3,
                                   indexOf(s.quality, qualityValues, 3, 0));
        s.quality = qualityValues[pick];
    }
    {
        static const wchar_t* langs[] = {L"Any", L"English", L"Japanese"};
        static const char* langValues[] = {"", "eng", "jpn"};
        const Rect ctrl = row(u, y, w, L"Preferred audio", nullptr);
        s.audioLang = langValues[segmented(u, 7210, ctrl, langs, 3,
                                           indexOf(s.audioLang, langValues, 3, 0))];
    }
    {
        static const wchar_t* langs[] = {L"Any", L"English", L"Japanese"};
        static const char* langValues[] = {"", "eng", "jpn"};
        const Rect ctrl = row(u, y, w, L"Preferred subtitles", nullptr);
        s.subLang = langValues[segmented(u, 7220, ctrl, langs, 3,
                                         indexOf(s.subLang, langValues, 3, 0))];
    }
    {
        const Rect ctrl = row(u, y, w, L"Subtitles on by default", nullptr);
        toggle(u, 7230, ctrl, s.subsOn);
    }
    {
        const Rect ctrl = row(u, y, w, L"Buffer ahead",
                              L"Seconds to pre-load. Empty or 0 measures your speed instead.");
        std::wstring v = s.bufferSeconds > 0 ? numText(s.bufferSeconds, false) : L"";
        field(u, st, 7240, ctrl, v, L"auto");
        s.bufferSeconds = v.empty() ? 0 : _wtof(v.c_str());
    }

    // ---- Downloads ----------------------------------------------------
    groupHeader(u, y, L"DOWNLOADS");
    {
        const Rect ctrl = row(u, y, w, L"Delete after watching",
                              L"The whole point. Turning this off keeps files on disk.");
        toggle(u, 7300, ctrl, s.deleteAfter);
    }
    {
        const Rect ctrl = row(u, y, w, L"Download folder", L"Empty uses a folder under %TEMP%.");
        std::wstring v = widen(s.savePath);
        field(u, st, 7310, ctrl, v, L"%TEMP%\\tsuzuki");
        s.savePath = narrow(v);
    }
    {
        const Rect ctrl = row(u, y, w, L"Speed limit", L"Megabits per second. 0 is unlimited.");
        std::wstring v = s.speedLimit > 0 ? numText(s.speedLimit, false) : L"";
        field(u, st, 7320, ctrl, v, L"unlimited");
        s.speedLimit = v.empty() ? 0 : _wtof(v.c_str());
    }
    {
        const Rect ctrl = row(u, y, w, L"Maximum connections", nullptr);
        std::wstring v = numText(s.maxConnections, true);
        field(u, st, 7330, ctrl, v, L"200");
        s.maxConnections = v.empty() ? 200 : _wtoi(v.c_str());
    }
    {
        const Rect ctrl = row(u, y, w, L"Fetch only what playback needs",
                              L"Gentler on the swarm; more likely to stall on a weak connection.");
        toggle(u, 7340, ctrl, s.streamedDownload);
    }

    // ---- Torrent ------------------------------------------------------
    groupHeader(u, y, L"TORRENT");
    {
        const Rect ctrl = row(u, y, w, L"Listen port", L"0 lets the client pick one.");
        std::wstring v = s.torrentPort > 0 ? numText(s.torrentPort, true) : L"";
        field(u, st, 7400, ctrl, v, L"automatic");
        s.torrentPort = v.empty() ? 0 : _wtoi(v.c_str());
    }
    {
        const Rect ctrl = row(u, y, w, L"Disable DHT", nullptr);
        toggle(u, 7410, ctrl, s.disableDHT);
    }
    {
        const Rect ctrl = row(u, y, w, L"Disable peer exchange", nullptr);
        toggle(u, 7420, ctrl, s.disablePeX);
    }

    // ---- Interface ----------------------------------------------------
    groupHeader(u, y, L"INTERFACE");
    {
        static const wchar_t* titles[] = {L"Romaji", L"English", L"Native"};
        static const char* titleValues[] = {"romaji", "english", "native"};
        const Rect ctrl = row(u, y, w, L"Title language", nullptr);
        s.titleLanguage = titleValues[segmented(u, 7500, ctrl, titles, 3,
                                                indexOf(s.titleLanguage, titleValues, 3, 0))];
    }
    {
        const Rect ctrl = row(u, y, w, L"Hide spoilers", L"Blurs synopses for unwatched episodes.");
        toggle(u, 7510, ctrl, s.hideSpoilers);
    }
    {
        const Rect ctrl = row(u, y, w, L"Show adult results", nullptr);
        toggle(u, 7520, ctrl, s.showAdult);
    }

    // ---- Privacy ------------------------------------------------------
    groupHeader(u, y, L"PRIVACY");
    {
        const Rect ctrl = row(u, y, w, L"DNS over HTTPS",
                              L"Resolves through this instead of your system resolver.");
        std::wstring v = widen(s.dohUrl);
        field(u, st, 7600, ctrl, v, L"system resolver");
        s.dohUrl = narrow(v);
    }
    {
        const Rect ctrl = row(u, y, w, L"Discord rich presence",
                              L"Shows what you are watching on your Discord profile.");
        toggle(u, 7610, ctrl, s.discordPresence);
    }
    if (s.discordPresence) {
        // Discord will not accept a presence without an application id, and
        // it has to be one you registered - it is the name Discord shows.
        // Make one at discord.com/developers, call it Tsuzuki, paste the
        // Application ID here.
        const Rect ctrl = row(u, y, w, L"Discord application ID",
                              L"From discord.com/developers - name the app whatever you want "
                              L"shown next to \"Watching\".");
        std::wstring v = widen(s.discordClientId);
        field(u, st, 7620, ctrl, v, L"required for presence");
        s.discordClientId = narrow(v);
    }

    y += 30;
    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Settings)] + 20;

    // Anything a control changed is written straight away; typing waits for a
    // short pause so a settings file is not rewritten on every keystroke.
    const bool changedByControl =
        before.syncProgress != s.syncProgress || before.quality != s.quality ||
        before.audioLang != s.audioLang || before.subLang != s.subLang ||
        before.subsOn != s.subsOn || before.deleteAfter != s.deleteAfter ||
        before.streamedDownload != s.streamedDownload || before.disableDHT != s.disableDHT ||
        before.disablePeX != s.disablePeX || before.titleLanguage != s.titleLanguage ||
        before.hideSpoilers != s.hideSpoilers || before.showAdult != s.showAdult ||
        before.discordPresence != s.discordPresence;

    if (changedByControl) {
        ui::applySettings(s);
        st.draftDirty = false;
    } else if (st.draftDirty) {
        if (GetTickCount() - st.draftAt > 700) {
            ui::applySettings(s);
            st.draftDirty = false;
        } else {
            wantMore = true;  // keep ticking until the pause is long enough
        }
    }

    if (st.focusField != 0) wantMore = true;  // caret blink
    return wantMore;
}

}  // namespace tsuzuki::view
