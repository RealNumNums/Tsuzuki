#include "view.hpp"

#include "../library.hpp"
#include "../track.hpp"
#include "../ui.hpp"
#include "async.hpp"
#include "images.hpp"
#include "mascot.hpp"

#include <algorithm>
#include <cmath>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

// Layout constants, in DIPs. Named rather than inline so the whole interface
// can be re-proportioned from one place.
constexpr float kPad = 26;        // page margin
constexpr float kHeaderH = 62;
// The navigation rail down the left edge. Everything else is laid out as
// though the window started here.
constexpr float kRailW = 64;
constexpr float kGap = 14;
constexpr float kCardW = 268;     // continue-watching card
constexpr float kTileW = 152;     // poster tile
constexpr float kRadius = 12;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

// Column count and width for a grid that fills `avail` with items of at least
// `minW`, so the layout reflows instead of leaving a ragged right edge.
struct Grid {
    int cols;
    float itemW;
};
Grid gridFor(float avail, float minW) {
    int cols = static_cast<int>((avail + kGap) / (minW + kGap));
    if (cols < 1) cols = 1;
    return {cols, (avail - kGap * (cols - 1)) / cols};
}

// A small x in the corner of a card. Registered after whatever it sits on,
// so when the pointer is over both, this one takes the press - the id that
// registers last during the press is the one the release is matched against.
bool dismiss(Ui& u, int id, const Rect& r, float reveal) {
    if (reveal < 0.05f) {
        u.clickable(id, r);  // still clickable, just not drawn yet
        return false;
    }
    const bool clicked = u.clickable(id, r);
    const float h = u.hover(id);
    u.c.fill(r, h > 0.1f ? bad.withAlpha(reveal) : shade.withAlpha(0.55f * reveal),
             r.w / 2);
    u.c.text(L"\u00D7", {r.x, r.y + 1, r.w, r.h}, gfx::rgb(0xFFFFFF, reveal),
             f(15, gfx::Weight::Bold, gfx::Align::Center));
    return clicked;
}

// "Clear" on the right of a section heading.
bool clearLink(Ui& u, int id, float y, float right) {
    const Rect r{right - 60, y - 3, 60, 24};
    const bool clicked = u.clickable(id, r);
    const float h = u.hover(id);
    u.c.text(L"Clear", {r.x, r.y + 3, r.w, 18}, h > 0.1f ? accent : dim,
             f(12, gfx::Weight::Medium, gfx::Align::Right));
    return clicked;
}

void sectionHeader(Ui& u, const std::wstring& title, const std::wstring& note, float x, float& y,
                   float w) {
    u.c.text(title, {x, y, w, 22}, fg, f(15.5f, gfx::Weight::Semibold));
    if (!note.empty()) {
        const float tw = 9 + title.size() * 8.0f;
        u.c.text(note, {x + tw, y + 3, w - tw, 20}, dim, f(12.5f));
    }
    y += 30;
}

// A cover with a dark scrim under it, so overlaid text stays readable whatever
// the artwork is doing.
void coverArt(Ui& u, const std::string& url, const Rect& r, float radius) {
    u.c.fill(r, card, radius);
    if (ID2D1Bitmap* bmp = images::get(url)) {
        u.c.image(bmp, r, radius);
    }
}

// The banner across the top of the home screen.
//
// It leads with whatever you would actually watch next rather than something
// trending, so the biggest thing on the screen is also the most useful button
// on it. Returns the height it drew, which is zero before AniList answers.
//
// Sized to most of the window, and shaded as little as the words need. A strip
// under a scrim heavy enough to read white text on anywhere shows almost none
// of the artwork it is made of, so the text is anchored along the bottom -
// where a fade into the page background has to exist regardless - and the rest
// of the picture is left at its own brightness.
float heroBanner(Ui& u, State& st, float top, bool& wantMore) {
    if (st.hero.id == 0 || st.hero.id != st.heroWant) return 0;

    const float w = u.c.bounds().w;
    const float winH = u.c.bounds().h;

    // Tall enough to be the picture, short enough that what is underneath
    // still peeks and says the page scrolls.
    const float heroH = (std::max)(340.0f, (std::min)(winH * 0.78f, 700.0f));
    // Reaches back under the navigation rail. The rail is drawn over the top
    // of it and lets it through, so the picture covers the window rather than
    // starting at an edge partway across it.
    const Rect band{-kRailW, top, w + kRailW, heroH};

    // The picture is drawn taller than the banner and faded out across the
    // whole of it, so it keeps going behind the rows underneath rather than
    // stopping at an edge. Fading the banner to the page colour and then
    // starting a second, separately-faded copy below it cannot line up - the
    // two gradients meet at different values and leave a visible step.
    constexpr float kBleed = 300;
    const Rect art{band.x, band.y, band.w, band.h + kBleed};

    u.c.fill(art, gfx::rgb(0x0E0E14));
    const std::string url = st.hero.banner.empty() ? st.hero.cover : st.hero.banner;
    if (ID2D1Bitmap* bmp = images::get(url)) u.c.image(bmp, art);

    // A soft wash from the left for the words, and one continuous fade into
    // the page that starts partway down the banner and finishes below it.
    // Across the whole picture, not just the banner's share of it - stopping
    // it at the banner's edge leaves a horizontal line where the wash ends.
    u.c.gradientH({art.x, art.y, art.w * 0.55f, art.h}, shade.withAlpha(0.62f),
                  shade.withAlpha(0.0f));
    const float fadeTop = band.y + band.h * 0.42f;
    u.c.gradient({art.x, fadeTop, art.w, art.bottom() - fadeTop}, bg.withAlpha(0.0f), bg);

    // ---- laid out upwards from the bottom edge ---------------------------
    const float bottom = band.bottom();
    const float leftW = (std::min)(660.0f, w * 0.52f);
    const float buttonY = bottom - 132;

    {
        const bool resuming = st.hero.percent > 0 && st.hero.percent < 95;
        wchar_t label[64];
        if (st.hero.episode > 0) {
            swprintf(label, 64, resuming ? L"Resume episode %d" : L"Watch episode %d",
                     st.hero.episode);
        } else {
            swprintf(label, 64, L"Watch now");
        }

        const Rect go{kPad, buttonY, 196, 42};
        const bool hot = u.clickable(710, go);
        const float h = u.hover(710);
        if (h > 0 && h < 1) wantMore = true;
        u.c.fill(go, h > 0.1f ? accentSoft : accent, 10);
        u.c.text(label, {go.x, go.y + 12, go.w, 20}, gfx::onAccent(),
                 f(13.5f, gfx::Weight::Semibold, gfx::Align::Center));

        if (resuming) {
            const Rect track{go.x, go.bottom() + 11, go.w, 3};
            u.c.fill(track, shade.withAlpha(0.45f), 1.5f);
            u.c.fill({track.x, track.y, track.w * st.hero.percent / 100.0f, track.h}, accent,
                     1.5f);
        }

        if (hot) {
            if (!st.hero.magnet.empty()) {
                st.lastAnilistId = st.hero.id;
                st.openMagnet = widen(st.hero.magnet);
                st.resumeEpisode = st.hero.episode;
                st.resumeSeconds = st.hero.resumeAt;
                st.openDone = false;
                st.searchDone = false;
                st.screen = Screen::Episodes;
                async::open(st.hero.magnet, st.hero.episode, st.hero.id);
            } else {
                // Nothing downloaded for it, so go and find one - for the
                // episode the button just named.
                st.query = widen(st.hero.title);
                if (st.hero.episode > 0) {
                    wchar_t ep[16];
                    swprintf(ep, 16, L"%d", st.hero.episode);
                    st.episodeWanted = ep;
                } else {
                    st.episodeWanted.clear();
                }
                st.lastAnilistId = st.hero.id;
                st.searchDone = false;
                st.pendingEpisode = st.hero.episode;
                st.screen = Screen::Results;
                async::search(st.hero.title, 0, st.hero.episode);
            }
        }
    }

    // ---- the facts, as chips, just above the button ----------------------
    const float chipsY = buttonY - 40;
    {
        float cx = kPad;
        std::vector<std::wstring> chips;
        if (st.hero.episodes > 0) {
            wchar_t s[24];
            swprintf(s, 24, L"%d episodes", st.hero.episodes);
            chips.push_back(s);
        }
        if (!st.hero.format.empty()) chips.push_back(widen(st.hero.format));
        if (st.hero.status == "RELEASING") chips.push_back(L"Airing");
        if (st.hero.year > 0) {
            wchar_t s[12];
            swprintf(s, 12, L"%d", st.hero.year);
            chips.push_back(s);
        }

        for (const auto& chip : chips) {
            const float pillW = static_cast<float>(chip.size()) * 6.8f + 20;
            const Rect pill{cx, chipsY, pillW, 26};
            u.c.fill(pill, shade.withAlpha(0.5f), 13);
            u.c.stroke(pill, fg.withAlpha(0.16f), 13);
            u.c.text(chip, {pill.x, pill.y + 5, pill.w, 16}, fg.withAlpha(0.9f),
                     f(11.5f, gfx::Weight::Medium, gfx::Align::Center));
            cx += pillW + 8;
        }
        if (st.hero.score > 0) {
            wchar_t sc[16];
            swprintf(sc, 16, L"%d%%", st.hero.score);
            const Rect pill{cx, chipsY, 52, 26};
            u.c.fill(pill, shade.withAlpha(0.5f), 13);
            u.c.stroke(pill, good.withAlpha(0.35f), 13);
            u.c.text(sc, {pill.x, pill.y + 5, pill.w, 16}, good,
                     f(11.5f, gfx::Weight::Semibold, gfx::Align::Center));
        }
    }

    // ---- title, sitting on top of the chips ------------------------------
    {
        Font title = f(40, gfx::Weight::Bold);
        title.wrap = true;
        const std::wstring name = widen(st.hero.title);
        const float titleH = (std::min)(140.0f, u.c.measure(name, leftW, title));
        u.c.text(name, {kPad, chipsY - 14 - titleH, leftW, titleH}, fg, title);
    }

    // ---- genres, bottom right --------------------------------------------
    const float genreY = bottom - 128;
    {
        // Measured first, then laid out left to right from a right-aligned
        // start. Walking leftwards while stepping through the genres in order
        // ends up drawing them backwards.
        const size_t shown = (std::min)(st.hero.genres.size(), static_cast<size_t>(3));
        std::vector<float> widths;
        float totalW = 0;
        for (size_t i = 0; i < shown; ++i) {
            const float pillW = static_cast<float>(widen(st.hero.genres[i]).size()) * 7.0f + 26;
            widths.push_back(pillW);
            totalW += pillW + (i + 1 < shown ? 9.0f : 0.0f);
        }

        float cx = w - kPad - totalW;
        for (size_t i = 0; i < shown; ++i) {
            const Rect pill{cx, genreY, widths[i], 30};
            cx += widths[i] + 9;
            const int id = 700 + static_cast<int>(i);
            const bool hot = u.clickable(id, pill);
            const float hv = u.hover(id);
            if (hv > 0 && hv < 1) wantMore = true;
            u.c.fill(pill, hv > 0.1f ? accent.withAlpha(0.3f) : shade.withAlpha(0.5f), 15);
            u.c.stroke(pill, hv > 0.1f ? accent : fg.withAlpha(0.16f), 15);
            u.c.text(widen(st.hero.genres[i]), {pill.x, pill.y + 7, pill.w, 16},
                     fg.withAlpha(0.9f), f(11.5f, gfx::Weight::Medium, gfx::Align::Center));
            if (hot) {
                st.genre = widen(st.hero.genres[i]);
                st.discoverLoaded = false;
                st.discovery = ui::Discovery{};
                st.scroll[static_cast<int>(Screen::Discover)] = 0;
                st.scrollTarget[static_cast<int>(Screen::Discover)] = 0;
                st.screen = Screen::Discover;
                async::discover(st.hero.genres[i]);
            }
        }
    }

    // ---- what it is about, above the genres ------------------------------
    if (!st.hero.description.empty()) {
        Font body = f(12.5f, gfx::Weight::Regular, gfx::Align::Right);
        body.wrap = true;
        const float dw = (std::min)(560.0f, w * 0.44f);
        u.c.text(widen(st.hero.description), {w - kPad - dw, genreY - 66, dw, 58},
                 fg.withAlpha(0.82f), body);
    }

    return heroH;
}

}  // namespace

// ---------------------------------------------------------------- helpers

std::wstring clipboardText() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return {};
    if (!OpenClipboard(nullptr)) return {};

    std::wstring out;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
            out = text;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();

    // A pasted magnet link often arrives with a trailing newline, and a single
    // line field should not grow a control character it cannot show.
    for (wchar_t& ch : out) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') ch = L' ';
    }
    while (!out.empty() && out.back() == L' ') out.pop_back();
    return out;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::wstring formatTime(double seconds) {
    if (!(seconds > 0)) seconds = 0;
    const int total = static_cast<int>(seconds);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    wchar_t buf[32];
    if (h > 0) {
        swprintf(buf, 32, L"%d:%02d:%02d", h, m, s);
    } else {
        swprintf(buf, 32, L"%d:%02d", m, s);
    }
    return buf;
}

// -------------------------------------------------------------------- Ui

void Ui::beginFrame() {
    hot_ = 0;
    animating_ = false;
}

void Ui::endFrame() {
    if (in.mouseReleased) pressed_ = 0;
}

void Ui::pushOrigin(float x, float y) {
    originX_ += x;
    originY_ += y;
    c.pushOrigin(x, y);
}

void Ui::popOrigin() {
    originX_ = originY_ = 0;
    c.popOrigin();
}

bool Ui::clickable(int id, const Rect& r) {
    // The mouse arrives in window coordinates; everything else in this frame
    // is in the current origin's coordinates.
    const float mx = in.mouseX - originX_;
    const float my = in.mouseY - originY_;
    const bool over = r.contains(mx, my) && my >= hitTop;
    if (over) hot_ = id;

    // Ease the hover value towards its target so cards lift smoothly instead
    // of snapping. Anything mid-transition asks for another frame.
    auto it = std::find_if(hoverAmount_.begin(), hoverAmount_.end(),
                           [id](const auto& p) { return p.first == id; });
    if (it == hoverAmount_.end()) {
        hoverAmount_.emplace_back(id, 0.0f);
        it = hoverAmount_.end() - 1;
    }
    const float target = over ? 1.0f : 0.0f;
    const float delta = target - it->second;
    if (std::fabs(delta) > 0.01f) {
        it->second += delta * 0.28f;
        animating_ = true;
    } else {
        it->second = target;
    }

    if (over && in.mousePressed) pressed_ = id;
    return over && in.mouseReleased && pressed_ == id;
}

float Ui::hover(int id) const {
    const auto it = std::find_if(hoverAmount_.begin(), hoverAmount_.end(),
                                 [id](const auto& p) { return p.first == id; });
    return it == hoverAmount_.end() ? 0.0f : it->second;
}

// ------------------------------------------------------------------- rail

namespace {

// The navigation rail down the left edge.
//
// Destinations live here rather than in the header because they are the one
// thing on screen that never changes, and a vertical rail gives the content
// its full width back. Drawn before the origin is moved, so it works in
// window coordinates while everything else works inset by kRailW.
// What the rail wants drawn on top of everything else once the frame is
// otherwise done. The rail itself is painted first, before the origin moves,
// which meant its tooltip was painted before the content and disappeared
// underneath it.
struct RailTip {
    const wchar_t* label = nullptr;
    float y = 0;
};

bool navRail(Ui& u, State& st, RailTip& tip) {
    const float h = u.c.bounds().h;
    bool wantMore = false;

    // Translucent, so whatever is behind it - usually the banner - carries on
    // through. Over a plain background it reads as a slightly darker strip,
    // which is what it used to be anyway.
    u.c.fill({0, 0, kRailW, h}, panel.withAlpha(0.62f));
    u.c.fill({kRailW - 1, 0, 1, h}, line.withAlpha(0.45f));

    // The mark, which doubles as the way home.
    {
        const Rect logo{(kRailW - 32) / 2, 16, 32, 32};
        const bool hot = u.clickable(9100, logo);
        u.c.gradient(logo, accentSoft, accent, 9);
        u.c.text(L"続", {logo.x, logo.y + 6, logo.w, 22}, gfx::onAccent(),
                 f(15, gfx::Weight::Bold, gfx::Align::Center));
        if (hot) st.screen = Screen::Home;
    }

    struct Item {
        const wchar_t* glyph;
        const wchar_t* label;
        Screen screen;
    };
    const Item items[] = {{gfx::icons::home, L"Home", Screen::Home},
                          {gfx::icons::compass, L"Discover", Screen::Discover},
                          {gfx::icons::calendar, L"Schedule", Screen::Schedule},
                          {gfx::icons::settings, L"Settings", Screen::Settings}};

    Font glyphFont = f(17, gfx::Weight::Regular, gfx::Align::Center);
    glyphFont.icon = true;

    // The mascot is a toggle rather than a destination, so it sits apart from
    // the four, just above Settings.
    {
        const Rect pill{(kRailW - 42) / 2, h - 112, 42, 40};
        const bool hot = u.clickable(9120, pill);
        const float hv = u.hover(9120);
        if (hv > 0 && hv < 1) wantMore = true;

        if (st.mascotOn) {
            u.c.fill(pill, accent, 11);
        } else if (hv > 0.05f) {
            u.c.fill(pill, cardHover.withAlpha(hv), 11);
        }
        Font g = f(17, gfx::Weight::Regular, gfx::Align::Center);
        g.icon = true;
        u.c.text(gfx::icons::mascot, {pill.x, pill.y + 11, pill.w, 22},
                 st.mascotOn ? gfx::onAccent() : (hv > 0.1f ? fg : dim), g);

        if (hv > 0.6f && !st.mascotOn) {
            tip.label = L"Mascot";
            tip.y = pill.y + 9;
        }
        if (hot) st.mascotOn = !st.mascotOn;
    }

    for (int i = 0; i < 4; ++i) {
        // Settings sits at the foot of the rail: it is the one destination you
        // are not moving between, so it should not be in the flow of the rest.
        const bool last = i == 3;
        const float top = last ? h - 62 : 76 + i * 50;
        const Rect pill{(kRailW - 42) / 2, top, 42, 40};

        const bool on = st.screen == items[i].screen ||
                        (items[i].screen == Screen::Discover && st.screen == Screen::Shelf);
        const bool hot = u.clickable(9110 + i, pill);
        const float hv = u.hover(9110 + i);
        if (hv > 0 && hv < 1) wantMore = true;

        if (on) {
            u.c.fill(pill, accent, 11);
        } else if (hv > 0.05f) {
            u.c.fill(pill, cardHover.withAlpha(hv), 11);
        }
        u.c.text(items[i].glyph, {pill.x, pill.y + 11, pill.w, 22},
                 on ? gfx::onAccent() : (hv > 0.1f ? fg : dim), glyphFont);

        // The label only appears when you go looking for it, so the rail stays
        // as quiet as the reference it is modelled on. Drawn later, on top.
        if (hv > 0.6f && !on) {
            tip.label = items[i].label;
            tip.y = pill.y + 9;
        }

        if (hot) {
            st.screen = items[i].screen;
            st.queryFocused = false;
            st.focusField = 0;
            if (items[i].screen == Screen::Settings) st.draftLoaded = false;
        }
    }

    return wantMore;
}

// The mascot, in the bottom-right corner.
//
// She is cut out of her background, so there is no panel and no frame - just
// the character over whatever the app is already painting. Painted last and
// deliberately not clickable: a big rectangle of mostly-transparent pixels
// swallowing clicks meant for the content underneath would be worse than
// having no way to dismiss her here, and the rail button is right there.
void drawMascot(Ui& u, const State& st) {
    if (!st.mascotOn) return;
    ID2D1Bitmap* art = mascot::art(u.c);
    if (!art) return;

    const Rect win = u.c.bounds();
    const D2D1_SIZE_F size = art->GetSize();
    if (size.width <= 0 || size.height <= 0) return;

    // Big enough to read as a character, small enough to stay a companion.
    float h = std::min(300.0f, win.h * 0.40f);
    float w = h * size.width / size.height;
    if (w > win.w * 0.42f) {
        w = win.w * 0.42f;
        h = w * size.height / size.width;
    }

    u.c.image(art, {win.w - w - 6, win.h - h, w, h});
}

// Painted after everything else, so nothing can cover it.
void railTooltip(Ui& u, const RailTip& tip) {
    if (!tip.label) return;
    const float tw = static_cast<float>(wcslen(tip.label)) * 6.8f + 20;
    const Rect box{kRailW + 8, tip.y, tw, 24};
    u.c.fill(box, cardHover, 6);
    u.c.stroke(box, line, 6);
    u.c.text(tip.label, {box.x, box.y + 4, box.w, 16}, fg,
             f(11.5f, gfx::Weight::Medium, gfx::Align::Center));
}

// ---------------------------------------------------------------- header

// Returns true if a search was submitted.
//
// `solid` fades the bar in. At zero it is not there at all and the artwork
// behind it runs to the top of the window, which is the whole point - a strip
// of opaque chrome above the picture makes the picture look like a panel
// dropped into a toolbar rather than like the page it is sitting on. It fades
// to opaque as the page scrolls, so text passing underneath stays readable.
bool header(Ui& u, State& st, float solid) {
    const float w = u.c.bounds().w;
    bool submit = false;

    if (solid > 0.01f) {
        u.c.fill({0, 0, w, kHeaderH}, panel.withAlpha(solid));
        u.c.fill({0, kHeaderH - 1, w, 1}, line.withAlpha(solid));
    }
    if (solid < 0.99f) {
        // Something for the controls to sit on while the bar itself is clear.
        u.c.gradient({0, 0, w, kHeaderH + 26}, shade.withAlpha(0.5f * (1 - solid)),
                     shade.withAlpha(0.0f));
    }

    // Search field. It has the header to itself now that the destinations
    // moved to the rail.
    const Rect box{kPad, 14, w - kPad * 2 - 180, 34};
    const bool overBox = box.contains(u.mouseX(), u.mouseY());
    if (u.in.mousePressed) st.queryFocused = overBox;
    // Over artwork the solid card colour looks pasted on; a smoked panel reads
    // as part of the picture.
    u.c.fill(box, solid > 0.5f ? card : shade.withAlpha(0.42f), 9);
    u.c.stroke(box, st.queryFocused ? accent : line.withAlpha(0.4f + 0.6f * solid), 9);

    const Rect textArea = {box.x + 12, box.y + 8, box.w - 24, 20};
    if (st.query.empty() && !st.queryFocused) {
        u.c.text(L"Search an anime, or paste a magnet link", textArea, dim, f(13.5f));
    } else {
        u.c.text(st.query, textArea, fg, f(13.5f));
        if (st.queryFocused && (GetTickCount() / 500) % 2 == 0) {
            const float cw = st.query.empty() ? 0 : st.query.size() * 6.6f;
            u.c.fill({textArea.x + cw + 1, textArea.y + 1, 1.4f, 17}, accent);
        }
    }

    // Episode box
    const Rect epBox{box.right() + 10, 14, 68, 34};
    u.c.fill(epBox, solid > 0.5f ? card : shade.withAlpha(0.42f), 9);
    u.c.stroke(epBox, line.withAlpha(0.4f + 0.6f * solid), 9);
    u.c.text(st.episodeWanted.empty() ? L"Ep #" : st.episodeWanted,
             {epBox.x + 10, epBox.y + 8, epBox.w - 16, 20},
             st.episodeWanted.empty() ? dim : fg, f(13.5f));

    // Search button
    const Rect go{epBox.right() + 10, 14, 92, 34};
    const bool hot = u.clickable(9001, go);
    u.c.gradient(go, hot ? accentSoft : accent, accent, 9);
    u.c.text(L"Search", {go.x, go.y + 8, go.w, 20}, gfx::onAccent(),
             f(13.5f, gfx::Weight::Semibold, gfx::Align::Center));
    if (hot) submit = true;

    if (st.queryFocused) {
        if (!u.in.typed.empty()) {
            for (const wchar_t ch : u.in.typed) {
                if (ch == kPasteChar) {
                    st.query += clipboardText();
                } else if (ch == L'\b') {
                    if (!st.query.empty()) st.query.pop_back();
                } else if (ch == L'\r' || ch == L'\n') {
                    submit = true;
                } else if (ch >= 32) {
                    st.query.push_back(ch);
                }
            }
        }
    }

    return submit;
}

void syncBadge(Ui& u) {
    const library::SyncStatus s = library::syncStatus();
    const std::wstring label = widen(s.label());
    const float w = u.c.bounds().w;

    Color dot = good;
    if (s.state == library::SyncState::NotLinked) {
        dot = dim.withAlpha(0.7f);
    } else if (s.state == library::SyncState::Retrying) {
        dot = bad;
    } else if (s.state == library::SyncState::Syncing || s.pending > 0) {
        dot = warn;
    }

    const float tw = label.size() * 6.4f + 22;
    const Rect r{w - kPad - tw, kHeaderH + 12, tw, 18};
    u.c.fill({r.x, r.y + 6, 7, 7}, dot, 3.5f);
    u.c.text(label, {r.x + 14, r.y, r.w - 14, 18}, dim, f(12));
}

}  // namespace

// ----------------------------------------------------------------- home

namespace {

bool home(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    const float avail = w - kPad * 2;
    bool wantMore = false;

    syncBadge(u);

    // Refreshed on a timer rather than per frame, and immediately whenever
    // something has changed them.
    {
        const long long now = static_cast<long long>(GetTickCount64());
        if (st.homeStale || now - st.homeFetchedAt > 1000) {
            st.homeContinue = library::continueWatching(12);
            st.homeList = library::cachedList();
            st.homeHistory = ui::history();
            st.homeFetchedAt = now;
            st.homeStale = false;
        }
    }
    const auto& cont = st.homeContinue;
    const auto& lists = st.homeList;
    const auto& hist = st.homeHistory;

    // ---- what the banner should be about ---------------------------------
    //
    // Whatever you would watch next: the thing you are partway through, or
    // failing that the next episode of something you are following. Not a
    // trending show, because the point of the banner is to be the shortest
    // path back to what you were doing.
    int heroEpisode = 0;
    int heroPercent = 0;
    double heroResume = 0;
    std::string heroMagnet;
    st.heroWant = 0;

    if (!cont.empty() && cont[0].anilistId > 0) {
        const auto& e = cont[0];
        st.heroWant = e.anilistId;
        heroEpisode = e.episode;
        heroPercent = static_cast<int>(e.percent());
        heroResume = e.currentTime;
        heroMagnet = e.magnet;
    } else {
        for (const auto& m : lists) {
            if (m.status == "CURRENT" && m.mediaId > 0) {
                st.heroWant = m.mediaId;
                heroEpisode = m.nextEpisode;
                break;
            }
        }
    }

    if (st.heroWant && st.heroAsked != st.heroWant && !async::spotlightRunning()) {
        st.heroAsked = st.heroWant;
        async::spotlight(st.heroWant);
    }
    async::takeSpotlight(st.hero);

    // AniList knows what the show is; only we know where you are in it.
    if (st.hero.id == st.heroWant) {
        st.hero.episode = heroEpisode;
        st.hero.percent = heroPercent;
        st.hero.resumeAt = heroResume;
        st.hero.magnet = heroMagnet;
    }

    // From the very top of the pane, not from under the header - the header
    // draws over it, fading in as this scrolls away.
    const float heroTop = -st.scroll[0];
    const float heroH = heroBanner(u, st, heroTop, wantMore);

    float y = (heroH > 0 ? heroH + 22 : kHeaderH + 40) - st.scroll[0];

    if (heroH == 0) {
        u.c.text(L"Downloads are deleted after you finish watching.",
                 {kPad, kHeaderH + 12, avail - 220, 18}, dim, f(12.5f));
    }

    if (cont.empty() && lists.empty() && hist.empty()) {
        u.c.text(L"Nothing here yet.", {kPad, kHeaderH + 80, avail, 26}, fg,
                 f(16, gfx::Weight::Semibold, gfx::Align::Center));
        u.c.text(L"Search for something, or see what is trending.",
                 {kPad, kHeaderH + 108, avail, 22}, dim,
                 f(13, gfx::Weight::Regular, gfx::Align::Center));

        const Rect go{u.c.bounds().cx() - 80, kHeaderH + 146, 160, 38};
        const bool hot = u.clickable(140, go);
        u.c.fill(go, u.hover(140) > 0.1f ? accentSoft : accent, 9);
        u.c.text(L"Discover", {go.x, go.y + 10, go.w, 20}, gfx::onAccent(),
                 f(13, gfx::Weight::Semibold, gfx::Align::Center));
        if (hot) st.screen = Screen::Discover;

        u.contentHeight = 0;
        return u.wantsAnimation();
    }

    // ---- Continue watching -------------------------------------------
    if (!cont.empty()) {
        sectionHeader(u, L"Continue watching", L"pick up where you left off", kPad, y, avail);
        if (clearLink(u, 190, y - 30, kPad + avail)) {
            library::forgetAllInProgress();
            st.homeStale = true;
            return true;  // the list just changed under us
        }
        const Grid g = gridFor(avail, kCardW);
        const float cardH = g.itemW * 9 / 16 + 62;

        for (size_t i = 0; i < cont.size(); ++i) {
            const auto& e = cont[i];
            const int col = static_cast<int>(i) % g.cols;
            const int row = static_cast<int>(i) / g.cols;
            Rect box{kPad + col * (g.itemW + kGap), y + row * (cardH + kGap), g.itemW, cardH};

            // Hit-tested where the card rests, then drawn lifted. Testing the
            // lifted rectangle meant hovering moved the card out from under the
            // pointer, which un-hovered it, which dropped it back - a card
            // under the cursor simply vibrated.
            const int id = 100 + static_cast<int>(i);
            const bool clicked = u.clickable(id, box);
            const float h = u.hover(id);
            if (h > 0) {
                box = box.offset(0, -2 * h);
                wantMore = true;
            }

            u.c.fill(box, h > 0.1f ? cardHover : card, kRadius);
            u.c.stroke(box, h > 0.1f ? accent : line, kRadius);

            const Rect art{box.x, box.y, box.w, box.w * 9 / 16};
            coverArt(u, e.cover, art, kRadius);
            u.c.gradient({art.x, art.bottom() - 54, art.w, 54}, shade.withAlpha(0.0f),
                         shade.withAlpha(0.82f));

            wchar_t badge[48];
            swprintf(badge, 48, L"Episode %d", e.episode);
            u.c.text(badge, {art.x + 10, art.bottom() - 30, art.w - 20, 18}, fg,
                     f(12, gfx::Weight::Semibold));
            u.c.text(formatTime(e.remaining()) + L" left",
                     {art.x + 10, art.bottom() - 30, art.w - 20, 18}, fg.withAlpha(0.88f),
                     f(11.5f, gfx::Weight::Regular, gfx::Align::Right));

            // Progress bar along the bottom of the artwork.
            const float pct = static_cast<float>(e.percent()) / 100.0f;
            u.c.fill({art.x, art.bottom() - 4, art.w, 4}, shade.withAlpha(0.55f));
            u.c.fill({art.x, art.bottom() - 4, art.w * pct, 4}, accent);

            u.c.text(widen(e.title), {box.x + 11, art.bottom() + 9, box.w - 22, 18}, fg,
                     f(13.5f, gfx::Weight::Semibold));
            u.c.text(formatTime(e.currentTime) + L" / " + formatTime(e.duration),
                     {box.x + 11, art.bottom() + 30, box.w - 22, 16}, dim, f(12));
            wchar_t pctText[24];
            swprintf(pctText, 24, L"%d%% watched", static_cast<int>(e.percent()));
            u.c.text(pctText, {box.x + 11, art.bottom() + 30, box.w - 22, 16}, dim,
                     f(12, gfx::Weight::Regular, gfx::Align::Right));

            // Registered after the card, so it wins the press when the
            // pointer is over both.
            const Rect x{box.right() - 32, box.y + 10, 22, 22};
            if (dismiss(u, 220 + static_cast<int>(i), x, h)) {
                library::forget(
                    library::keyFor(e.anilistId, e.episode, e.infoHash, e.fileIndex));
                st.homeStale = true;
                return true;
            }

            if (clicked && !e.magnet.empty()) {
                st.lastAnilistId = e.anilistId;
                st.openMagnet = widen(e.magnet);
                st.resumeEpisode = e.episode;
                st.resumeSeconds = e.currentTime;
                st.openDone = false;
                st.searchDone = false;
                st.screen = Screen::Episodes;
                async::open(e.magnet, e.episode, e.anilistId);
            }
        }
        const int rows = (static_cast<int>(cont.size()) + g.cols - 1) / g.cols;
        y += rows * (cardH + kGap) + 12;
    }

    // ---- From your AniList --------------------------------------------
    std::vector<library::CachedMedia> watching;
    for (const auto& e : lists) {
        if (e.status == "CURRENT" || e.status == "REPEATING") watching.push_back(e);
    }
    if (!watching.empty()) {
        sectionHeader(u, L"From your AniList", L"currently watching", kPad, y, avail);
        const Grid g = gridFor(avail, kTileW);
        const float tileH = g.itemW * 1.42f + 40;

        for (size_t i = 0; i < watching.size(); ++i) {
            const auto& e = watching[i];
            const int col = static_cast<int>(i) % g.cols;
            const int row = static_cast<int>(i) / g.cols;
            Rect tile{kPad + col * (g.itemW + kGap), y + row * (tileH + kGap), g.itemW, tileH};

            const int id = 300 + static_cast<int>(i);
            const bool clicked = u.clickable(id, tile);
            const float h = u.hover(id);
            if (h > 0) {
                tile = tile.offset(0, -3 * h);
                wantMore = true;
            }

            const Rect art{tile.x, tile.y, tile.w, tile.w * 1.42f};
            coverArt(u, e.cover, art, 10);
            u.c.stroke(art, h > 0.1f ? accent : line, 10);
            u.c.gradient({art.x, art.bottom() - 48, art.w, 48}, shade.withAlpha(0.0f),
                         shade.withAlpha(0.8f));

            wchar_t next[64];
            if (e.episodes > 0) {
                swprintf(next, 64, L"Episode %d of %d", e.nextEpisode, e.episodes);
            } else {
                swprintf(next, 64, L"Episode %d", e.nextEpisode);
            }
            u.c.text(next, {art.x + 8, art.bottom() - 26, art.w - 16, 18}, fg,
                     f(11.5f, gfx::Weight::Semibold));

            if (e.episodes > 0) {
                const float pct = static_cast<float>(e.progress) / e.episodes;
                u.c.fill({art.x, art.bottom() - 3, art.w, 3}, shade.withAlpha(0.5f));
                u.c.fill({art.x, art.bottom() - 3, art.w * pct, 3}, accent);
            }

            Font name = f(12.5f, gfx::Weight::Medium);
            name.wrap = true;
            u.c.text(widen(e.title), {tile.x, art.bottom() + 8, tile.w, 34}, fg, name);

            if (clicked) {
                st.query = widen(e.title);
                wchar_t ep[16];
                swprintf(ep, 16, L"%d", e.nextEpisode);
                st.episodeWanted = ep;
                st.lastAnilistId = e.mediaId;
                st.searchDone = false;
                st.screen = Screen::Results;
                st.pendingEpisode = e.nextEpisode;
                async::search(e.title, 0, e.nextEpisode);
            }
        }
        const int rows = (static_cast<int>(watching.size()) + g.cols - 1) / g.cols;
        y += rows * (tileH + kGap) + 12;
    }

    // ---- Recently opened ----------------------------------------------
    if (!hist.empty()) {
        sectionHeader(u, L"Recently opened", L"on this machine", kPad, y, avail);
        if (clearLink(u, 191, y - 30, kPad + avail)) {
            ui::clearHistory();
            st.homeStale = true;
            return true;
        }
        for (size_t i = 0; i < hist.size() && i < 12; ++i) {
            const auto& h = hist[i];
            const Rect row{kPad, y, avail, 66};
            const int id = 500 + static_cast<int>(i);
            const float hv = u.hover(id);
            if (hv > 0) wantMore = true;
            const bool clicked = u.clickable(id, row);

            u.c.fill(row, hv > 0.1f ? cardHover : card, kRadius);
            u.c.stroke(row, hv > 0.1f ? accent : line, kRadius);

            const Rect thumb{row.x + 10, row.y + 9, 84, 48};
            coverArt(u, h.cover, thumb, 7);
            if (h.episode > 0) {
                wchar_t ep[16];
                swprintf(ep, 16, L"EP %d", h.episode);
                u.c.gradient(thumb, shade.withAlpha(0.0f), shade.withAlpha(0.6f), 7);
                u.c.text(ep, {thumb.x, thumb.y + 16, thumb.w, 18}, fg,
                         f(12, gfx::Weight::Bold, gfx::Align::Center));
            }

            const float tx = thumb.right() + 12;
            u.c.text(widen(h.show.empty() ? h.torrent : h.show), {tx, row.y + 12, row.w - tx, 20},
                     fg, f(13.5f, gfx::Weight::Semibold));
            u.c.text(widen(h.episode > 0 ? "Episode " + std::to_string(h.episode) : h.file),
                     {tx, row.y + 32, row.w - tx - 150, 18}, accent.withAlpha(0.9f), f(12));
            u.c.text(widen(h.torrent), {tx, row.y + 47, row.w - tx - 150, 16}, dim, f(11.5f));

            const Rect open{row.right() - 130, row.y + 18, 80, 30};
            u.c.fill(open, hv > 0.1f ? accent : cardHover, 8);
            u.c.text(L"Open", {open.x, open.y + 7, open.w, 18},
                     hv > 0.1f ? gfx::onAccent() : fg,
                     f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));

            const Rect hx{row.right() - 38, row.y + 22, 22, 22};
            if (dismiss(u, 560 + static_cast<int>(i), hx, hv)) {
                ui::forgetHistory(h.magnet, h.file);
                st.homeStale = true;
                return true;
            }

            if (clicked) {
                st.lastAnilistId = h.anilistId;
                st.openMagnet = widen(h.magnet);
                st.openDone = false;
                st.searchDone = false;
                st.screen = Screen::Episodes;
                async::open(h.magnet, h.episode, h.anilistId);
            }
            y += 66 + 9;
        }
    }

    u.contentHeight = y + st.scroll[0] + 30;
    return wantMore;
}

}  // namespace

bool frame(Ui& u, State& st) {
    bool wantMore = false;
    u.beginFrame();

    if (st.screen == Screen::Player) {
        const bool more = playerStrip(u, st);
        u.contentHeight = 0;
        u.endFrame();
        return more;
    }

    // The origin moves first and the rail is drawn last, over the top. Drawn
    // first it would be painted under the banner - and the point is for the
    // banner to show through it.
    RailTip tip;
    u.pushOrigin(kRailW, 0);

    // Content first, clipped to its own area, then the header painted over
    // the top. Without the clip, a scrolled page drew its rows straight
    // through the header and the two overlapped.
    const Rect full = u.c.bounds();
    // Clipped to the whole window rather than to the pane below the header:
    // the banner has to reach the top edge, and back under the rail. Nothing
    // else draws that far left - every other screen starts at kPad - and the
    // rail paints over the strip afterwards regardless. Clicks are still kept
    // out of the header's band by hitTop.
    u.c.pushClip({-kRailW, 0, full.w + kRailW, full.h});
    u.hitTop = kHeaderH;

    switch (st.screen) {
        case Screen::Home: wantMore = home(u, st); break;
        case Screen::Settings: wantMore = settingsScreen(u, st); break;
        case Screen::Results: wantMore = resultsScreen(u, st); break;
        case Screen::Episodes: wantMore = episodesScreen(u, st); break;
        case Screen::Discover: wantMore = discoverScreen(u, st); break;
        case Screen::Shelf: wantMore = shelfScreen(u, st); break;
        case Screen::Schedule: wantMore = scheduleScreen(u, st); break;
        default: break;
    }

    u.c.popClip();
    u.hitTop = 0;

    // Transparent over the banner, opaque once the page has scrolled past it
    // or when there is no banner to be transparent over.
    float headerSolid = 1.0f;
    if (st.screen == Screen::Home && st.hero.id != 0 && st.hero.id == st.heroWant) {
        headerSolid = (std::min)(1.0f, (std::max)(0.0f, st.scroll[0] / 130.0f));
    }

    if (header(u, st, headerSolid) && !st.query.empty()) {
        // A magnet link pasted into the search box skips straight to the
        // file list - there is nothing to look up.
        const std::wstring q = st.query;
        const int wantEp = st.episodeWanted.empty() ? 0 : _wtoi(st.episodeWanted.c_str());
        if (q.rfind(L"magnet:", 0) == 0) {
            st.openMagnet = q;
            st.lastAnilistId = 0;
            st.openDone = false;
            st.searchDone = false;
            st.screen = Screen::Episodes;
            async::open(narrow(q), wantEp, 0);
        } else {
            st.searchDone = false;
            st.screen = Screen::Results;
            st.pendingEpisode = wantEp;
            async::search(narrow(q), 0, wantEp);
        }
    }

    u.popOrigin();
    wantMore |= navRail(u, st, tip);
    drawMascot(u, st);
    railTooltip(u, tip);
    u.endFrame();
    return wantMore || u.wantsAnimation();
}

}  // namespace tsuzuki::view
