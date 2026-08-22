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
           const wchar_t* placeholder, bool secret = false) {
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
        // A password is shown as dots. It is still held in memory as typed -
        // it has to be, to be sent - but it should not be on screen.
        u.c.text(secret ? std::wstring(value.size(), L'\u2022') : value, inner, fg, f(12.5f));
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

    // ---- Accounts -----------------------------------------------------
    groupHeader(u, y, L"ACCOUNTS");
    {
        int id = 7100;
        for (const ui::TrackerRow& t : ui::trackers()) {
            const std::wstring name = widen(t.name);
            const std::wstring note =
                t.linked ? (t.account.empty() ? std::wstring(L"Linked.")
                                              : L"Signed in as " + widen(t.account))
                : !t.configured ? widen(t.hint)
                                : std::wstring(L"Progress is sent here as you watch.");

            // row() takes a const wchar_t*, and these are built per frame.
            const Rect ctrl = row(u, y, w, name.c_str(), note.c_str());
            const Rect btn{ctrl.right() - 150, ctrl.y, 150, 32};

            const bool canPress = t.linked || t.configured;
            const bool clicked = canPress && u.clickable(id, btn);
            u.c.fill(btn, !canPress ? gfx::rgb(0x1A1A24)
                        : t.linked  ? gfx::rgb(0x22222E)
                                    : accent,
                     8);
            u.c.text(t.linked ? L"Unlink" : L"Link", {btn.x, btn.y + 8, btn.w, 18},
                     !canPress ? dim : (t.linked ? fg : gfx::rgb(0x2A0D18)),
                     f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));

            if (clicked) {
                st.linkError.clear();
                if (t.linked) {
                    ui::unlinkTracker(t.id);
                    if (st.linking == t.id) st.linking.clear();
                } else {
                    const ui::LinkStart r = ui::startLink(t.id);
                    if (!r.ok) {
                        st.linkError = widen(r.error);
                    } else if (t.authKind == 1) {  // device code
                        st.linking = t.id;
                        st.deviceCode = widen(r.userCode);
                        st.deviceUrl = widen(r.verificationUrl);
                    } else if (t.authKind == 2) {  // password
                        st.linking = t.id;
                    }
                }
            }
            id += 2;

            // A service that cannot be linked because nobody has supplied a
            // client id needs somewhere to put one, or the hint above is a
            // dead end.
            if (!t.configured) {
                std::wstring value = widen(t.id == "mal" ? s.malClientId : s.simklClientId);
                const Rect box{w - kPad - kCtrlW, y, kCtrlW, 30};
                field(u, st, t.id == "mal" ? 7810 : 7811, box, value,
                      L"paste the Client ID");
                u.c.text(L"Client ID", {kPad, y + 6, 200, 18}, dim, f(11.5f));
                if (t.id == "mal") {
                    s.malClientId = narrow(value);
                } else {
                    s.simklClientId = narrow(value);
                }
                y += 40;
            }

            // ---- the two flows that need more than a button -----------
            if (st.linking == t.id && t.authKind == 1 && !st.deviceCode.empty()) {
                u.c.fill({kPad, y, w - kPad * 2, 62}, gfx::rgb(0x16161F), 10);
                u.c.stroke({kPad, y, w - kPad * 2, 62}, accent.withAlpha(0.5f), 10);
                u.c.text(L"Enter this code at " + st.deviceUrl,
                         {kPad + 16, y + 10, w - kPad * 2 - 32, 18}, dim, f(11.5f));
                u.c.text(st.deviceCode, {kPad + 16, y + 28, 300, 26}, accent,
                         f(20, gfx::Weight::Bold));

                // Ask Simkl every couple of seconds whether it has been
                // approved. Any faster is just noise on their end.
                const unsigned now = GetTickCount();
                if (now - st.lastPoll > 2500) {
                    st.lastPoll = now;
                    std::string err;
                    if (ui::pollLink(t.id, err)) {
                        st.linking.clear();
                        st.deviceCode.clear();
                    } else if (!err.empty()) {
                        st.linkError = widen(err);
                    }
                }
                wantMore = true;  // keep polling
                y += 72;
            }

            if (st.linking == t.id && t.authKind == 2) {
                u.c.fill({kPad, y, w - kPad * 2, 108}, gfx::rgb(0x16161F), 10);
                u.c.stroke({kPad, y, w - kPad * 2, 108}, line, 10);
                u.c.text(widen(t.hint), {kPad + 16, y + 10, w - kPad * 2 - 32, 18}, dim,
                         f(11.5f));

                const float fieldW = (std::min)(320.0f, w - kPad * 2 - 200);
                u.c.text(L"Username", {kPad + 16, y + 38, 90, 18}, fg, f(12));
                field(u, st, 7800, {kPad + 110, y + 32, fieldW, 30}, st.kitsuUser, L"");
                u.c.text(L"Password", {kPad + 16, y + 74, 90, 18}, fg, f(12));
                field(u, st, 7801, {kPad + 110, y + 68, fieldW, 30}, st.kitsuPassword, L"",
                      true);

                const Rect go{kPad + 130 + fieldW, y + 50, 110, 32};
                if (u.clickable(7802, go)) {
                    std::string err;
                    if (ui::signInTracker(t.id, narrow(st.kitsuUser),
                                          narrow(st.kitsuPassword), err)) {
                        st.linking.clear();
                    } else {
                        st.linkError = widen(err);
                    }
                    // Never keep it around a moment longer than needed.
                    st.kitsuPassword.clear();
                }
                u.c.fill(go, accent, 8);
                u.c.text(L"Sign in", {go.x, go.y + 7, go.w, 18}, gfx::rgb(0x2A0D18),
                         f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));
                y += 118;
            }
        }

        if (!st.linkError.empty()) {
            u.c.text(st.linkError, {kPad, y, w - kPad * 2, 18}, bad, f(11.5f));
            y += 22;
        }
    }
    {
        const Rect ctrl = row(u, y, w, L"Sync progress",
                              L"Marks an episode watched on every linked service "
                              L"once you reach the end.");
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
        // Say plainly whether Discord accepted us. Presence failing silently
        // is the whole reason this was broken and nobody could tell.
        const ui::Status st2 = ui::status();
        const bool live = st2.discordConnected;
        u.c.fill({kPad, y + 4, 7, 7}, live ? good : gfx::rgb(0x5A5A68), 3.5f);
        u.c.text(live ? L"Connected to Discord"
                      : L"Not connected - is Discord running?",
                 {kPad + 14, y - 4, w - kPad * 2, 18}, dim, f(11.5f));
        y += 22;

        // Optional. Tsuzuki ships its own application id, so this is only
        // for pointing the presence at a different Discord app.
        const Rect ctrl = row(u, y, w, L"Discord application ID",
                              L"Optional - leave empty to use Tsuzuki's own. "
                              L"Yours goes here if you would rather it showed your app.");
        std::wstring v = widen(s.discordClientId);
        field(u, st, 7620, ctrl, v, L"using Tsuzuki's own");
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
