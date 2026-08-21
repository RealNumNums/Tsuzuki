#include "view.hpp"

#include "../library.hpp"
#include "../track.hpp"
#include "../ui.hpp"
#include "images.hpp"

#include <algorithm>
#include <cmath>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

// Layout constants, in DIPs. Named rather than inline so the whole interface
// can be re-proportioned from one place.
constexpr float kPad = 26;        // page margin
constexpr float kHeaderH = 62;
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
    u.c.fill(r, gfx::rgb(0x12121A), radius);
    if (ID2D1Bitmap* bmp = images::get(url)) {
        u.c.image(bmp, r, radius);
    }
}

}  // namespace

// ---------------------------------------------------------------- helpers

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

bool Ui::clickable(int id, const Rect& r) {
    const bool over = r.contains(in.mouseX, in.mouseY);
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

// ---------------------------------------------------------------- header

namespace {

// Returns true if a search was submitted.
bool header(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    bool submit = false;

    u.c.fill({0, 0, w, kHeaderH}, gfx::rgb(0x0E0E14));
    u.c.fill({0, kHeaderH - 1, w, 1}, line);

    // Wordmark
    u.c.text(L"Tsuzuki", {kPad, 20, 110, 24}, fg, f(17, gfx::Weight::Bold));
    u.c.text(L"続き", {kPad + 78, 24, 60, 20}, accent.withAlpha(0.85f), f(13));

    // Search field
    const Rect box{kPad + 150, 14, w - kPad * 2 - 150 - 240, 34};
    const bool overBox = box.contains(u.in.mouseX, u.in.mouseY);
    if (u.in.mousePressed) st.queryFocused = overBox;
    u.c.fill(box, gfx::rgb(0x16161F), 9);
    u.c.stroke(box, st.queryFocused ? accent : line, 9);

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
    u.c.fill(epBox, gfx::rgb(0x16161F), 9);
    u.c.stroke(epBox, line, 9);
    u.c.text(st.episodeWanted.empty() ? L"Ep #" : st.episodeWanted,
             {epBox.x + 10, epBox.y + 8, epBox.w - 16, 20},
             st.episodeWanted.empty() ? dim : fg, f(13.5f));

    // Search button
    const Rect go{epBox.right() + 10, 14, 92, 34};
    const bool hot = u.clickable(9001, go);
    u.c.gradient(go, hot ? accentSoft : accent, accent, 9);
    u.c.text(L"Search", {go.x, go.y + 8, go.w, 20}, gfx::rgb(0x2A0D18),
             f(13.5f, gfx::Weight::Semibold, gfx::Align::Center));
    if (hot) submit = true;

    if (st.queryFocused) {
        if (!u.in.typed.empty()) {
            for (const wchar_t ch : u.in.typed) {
                if (ch == L'\b') {
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
        dot = gfx::rgb(0x5A5A68);
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
    float y = kHeaderH + 40 - st.scroll[0];
    bool wantMore = false;

    u.c.text(L"Downloads are deleted after you finish watching.",
             {kPad, kHeaderH + 12, avail - 220, 18}, dim, f(12.5f));
    syncBadge(u);

    const auto cont = library::continueWatching(12);
    const auto lists = library::cachedList();
    const auto hist = ui::history();

    if (cont.empty() && lists.empty() && hist.empty()) {
        u.c.text(L"Search for something to watch.", {kPad, kHeaderH + 90, avail, 24}, dim,
                 f(15, gfx::Weight::Regular, gfx::Align::Center));
        u.contentHeight = 0;
        return false;
    }

    // ---- Continue watching -------------------------------------------
    if (!cont.empty()) {
        sectionHeader(u, L"Continue watching", L"pick up where you left off", kPad, y, avail);
        const Grid g = gridFor(avail, kCardW);
        const float cardH = g.itemW * 9 / 16 + 62;

        for (size_t i = 0; i < cont.size(); ++i) {
            const auto& e = cont[i];
            const int col = static_cast<int>(i) % g.cols;
            const int row = static_cast<int>(i) / g.cols;
            Rect box{kPad + col * (g.itemW + kGap), y + row * (cardH + kGap), g.itemW, cardH};

            const int id = 100 + static_cast<int>(i);
            const float h = u.hover(id);
            if (h > 0) {
                box = box.offset(0, -2 * h);
                wantMore = true;
            }
            const bool clicked = u.clickable(id, box);

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
                     {art.x + 10, art.bottom() - 30, art.w - 20, 18}, gfx::rgb(0xDCDCE6),
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

            if (clicked && !e.magnet.empty()) {
                st.lastAnilistId = e.anilistId;
                st.openMagnet = widen(e.magnet);
                st.resumeEpisode = e.episode;
                st.resumeSeconds = e.currentTime;
                st.screen = Screen::Episodes;
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
            const float h = u.hover(id);
            if (h > 0) {
                tile = tile.offset(0, -3 * h);
                wantMore = true;
            }
            const bool clicked = u.clickable(id, tile);

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
                st.screen = Screen::Results;
            }
        }
        const int rows = (static_cast<int>(watching.size()) + g.cols - 1) / g.cols;
        y += rows * (tileH + kGap) + 12;
    }

    // ---- Recently opened ----------------------------------------------
    if (!hist.empty()) {
        sectionHeader(u, L"Recently opened", L"on this machine", kPad, y, avail);
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
                     {tx, row.y + 32, row.w - tx - 110, 18}, accent.withAlpha(0.9f), f(12));
            u.c.text(widen(h.torrent), {tx, row.y + 47, row.w - tx - 110, 16}, dim, f(11.5f));

            const Rect open{row.right() - 96, row.y + 18, 80, 30};
            u.c.fill(open, hv > 0.1f ? accent : gfx::rgb(0x22222E), 8);
            u.c.text(L"Open", {open.x, open.y + 7, open.w, 18},
                     hv > 0.1f ? gfx::rgb(0x2A0D18) : fg,
                     f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));

            if (clicked) {
                st.lastAnilistId = h.anilistId;
                st.openMagnet = widen(h.magnet);
                st.screen = Screen::Episodes;
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

    if (header(u, st) && !st.query.empty()) {
        st.screen = Screen::Results;
    }

    switch (st.screen) {
        case Screen::Home: wantMore = home(u, st); break;
        default:
            u.c.text(L"Coming next: results, episodes, settings.",
                     {kPad, kHeaderH + 60, u.c.bounds().w - kPad * 2, 24}, dim, f(14));
            break;
    }

    u.endFrame();
    return wantMore || u.wantsAnimation();
}

}  // namespace tsuzuki::view
