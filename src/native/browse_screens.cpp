// Search results, the episode list, and the resume prompt.
//
// Between them these are the path from "I want to watch this" to a running
// player, so they carry the two rules the project exists for: show what was
// actually matched, and never quietly play a file nobody asked for.

#include "../library.hpp"
#include "../ui.hpp"
#include "async.hpp"
#include "images.hpp"
#include "view.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

constexpr float kPad = 26;
constexpr float kHeaderH = 62;
constexpr float kRadius = 12;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

std::wstring sizeText(long long bytes) {
    if (bytes <= 0) return L"";
    const double gb = static_cast<double>(bytes) / (1024.0 * 1024 * 1024);
    wchar_t buf[32];
    if (gb >= 1.0) {
        swprintf(buf, 32, L"%.2f GB", gb);
    } else {
        swprintf(buf, 32, L"%.0f MB", gb * 1024);
    }
    return buf;
}

// A small rounded label - source name, "BEST", seeder count.
void chip(Ui& u, float& x, float y, const std::wstring& text, Color bg, Color fgc) {
    const float w = text.size() * 6.4f + 16;
    u.c.fill({x, y, w, 19}, bg, 5);
    u.c.text(text, {x, y + 2, w, 16}, fgc, f(11, gfx::Weight::Medium, gfx::Align::Center));
    x += w + 6;
}

void backButton(Ui& u, State& st, float y, Screen to, const wchar_t* label) {
    const Rect b{kPad, y, 150, 30};
    const bool hot = u.clickable(8000, b);
    u.c.fill(b, u.hover(8000) > 0.1f ? gfx::rgb(0x22222E) : gfx::rgb(0x16161F), 8);
    u.c.stroke(b, line, 8);
    u.c.text(label, {b.x, b.y + 6, b.w, 18}, dim, f(12.5f, gfx::Weight::Medium, gfx::Align::Center));
    if (hot) st.screen = to;
}

// Spinner-free busy line: this work takes seconds and a moving dot adds
// nothing over saying what is happening.
void busy(Ui& u, const std::wstring& text) {
    u.c.text(text, {kPad, kHeaderH + 90, u.c.bounds().w - kPad * 2, 24}, dim,
             f(14.5f, gfx::Weight::Regular, gfx::Align::Center));
}

}  // namespace

// ------------------------------------------------------------------ results

bool resultsScreen(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    const float avail = w - kPad * 2;
    bool wantMore = false;

    if (async::takeSearch(st.results)) st.searchDone = true;

    if (async::searchRunning()) {
        busy(u, L"Searching every source for " + st.query + L"...");
        u.contentHeight = 0;
        return true;
    }
    if (!st.searchDone) {
        busy(u, L"Nothing searched yet.");
        u.contentHeight = 0;
        return false;
    }
    if (!st.results.error.empty()) {
        busy(u, widen(st.results.error));
        u.contentHeight = 0;
        return false;
    }

    float y = kHeaderH + 22 - st.scroll[static_cast<int>(Screen::Results)];
    backButton(u, st, y, Screen::Home, L"< Back to home");
    y += 44;

    const std::wstring heading =
        st.results.resolvedTitle.empty() ? st.query : widen(st.results.resolvedTitle);
    u.c.text(heading, {kPad, y, avail, 30}, fg, f(21, gfx::Weight::Bold));
    y += 32;

    {
        wchar_t sub[160];
        if (st.results.episodes > 0) {
            swprintf(sub, 160, L"%d releases  -  %d episodes",
                     static_cast<int>(st.results.results.size()), st.results.episodes);
        } else {
            swprintf(sub, 160, L"%d releases", static_cast<int>(st.results.results.size()));
        }
        u.c.text(sub, {kPad, y, avail, 20}, dim, f(12.5f));
        y += 30;
    }

    if (st.results.results.empty()) {
        u.c.text(L"No releases found. Try a different title, or paste a magnet link.",
                 {kPad, y + 20, avail, 24}, dim, f(14));
        u.contentHeight = y + 80 + st.scroll[static_cast<int>(Screen::Results)];
        return false;
    }

    for (size_t i = 0; i < st.results.results.size(); ++i) {
        const auto& r = st.results.results[i];
        const Rect row{kPad, y, avail, 62};
        const int id = 1000 + static_cast<int>(i);
        const bool clicked = u.clickable(id, row);
        const float h = u.hover(id);
        if (h > 0) wantMore = true;

        u.c.fill(row, h > 0.1f ? cardHover : card, kRadius);
        u.c.stroke(row, h > 0.1f ? accent : (r.curatedBest ? accent.withAlpha(0.45f) : line),
                   kRadius);

        u.c.text(widen(r.title), {row.x + 14, row.y + 11, row.w - 150, 20}, fg,
                 f(13, gfx::Weight::Medium));

        float cx = row.x + 14;
        const float cy = row.y + 33;
        if (r.curatedBest) chip(u, cx, cy, L"BEST", accent, gfx::rgb(0x2A0D18));
        if (!r.sourceId.empty()) {
            chip(u, cx, cy, widen(r.sourceId), gfx::rgb(0x22222E), dim);
        }
        if (r.seeders > 0) {
            wchar_t s[24];
            swprintf(s, 24, L"%d seeders", r.seeders);
            chip(u, cx, cy, s, gfx::rgb(0x1C2A1F), good);
        }
        if (r.size > 0) chip(u, cx, cy, sizeText(r.size), gfx::rgb(0x22222E), dim);

        if (clicked) {
            st.openMagnet = widen(r.magnet);
            st.lastAnilistId = st.results.anilistId;
            st.openDone = false;
            st.screen = Screen::Episodes;
            const int wantEp = st.episodeWanted.empty() ? 0 : _wtoi(st.episodeWanted.c_str());
            async::open(narrow(st.openMagnet), wantEp, st.lastAnilistId);
        }
        y += 62 + 8;
    }

    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Results)] + 20;
    return wantMore;
}

// ----------------------------------------------------------------- episodes

bool episodesScreen(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    const float avail = w - kPad * 2;
    bool wantMore = false;

    if (async::takeOpen(st.opened)) st.openDone = true;

    if (async::openRunning()) {
        return waitingPanel(u, st, L"Finding this release");
    }
    if (!st.openDone) {
        busy(u, L"Nothing open.");
        u.contentHeight = 0;
        return false;
    }
    if (!st.opened.error.empty()) {
        busy(u, widen(st.opened.error));
        u.contentHeight = 0;
        return false;
    }

    const auto& o = st.opened;
    float y = kHeaderH + 22 - st.scroll[static_cast<int>(Screen::Episodes)];
    backButton(u, st, y, st.searchDone ? Screen::Results : Screen::Home,
               st.searchDone ? L"< Back to results" : L"< Back to home");
    y += 44;

    // ---- hero ---------------------------------------------------------
    if (!o.title.empty()) {
        const Rect hero{kPad, y, avail, 150};
        u.c.fill(hero, gfx::rgb(0x12121A), kRadius);
        if (!o.banner.empty()) {
            if (ID2D1Bitmap* b = images::get(o.banner)) u.c.image(b, hero, kRadius, 0.42f);
        }
        u.c.stroke(hero, line, kRadius);

        const Rect cover{hero.x + 14, hero.y + 14, 88, 122};
        if (!o.cover.empty()) {
            u.c.fill(cover, gfx::rgb(0x0E0E14), 8);
            if (ID2D1Bitmap* b = images::get(o.cover)) u.c.image(b, cover, 8);
        }

        const float tx = cover.right() + 16;
        u.c.text(widen(o.title), {tx, hero.y + 16, hero.w - (tx - hero.x) - 16, 28}, fg,
                 f(19, gfx::Weight::Bold));

        wchar_t facts[200];
        if (o.episodes > 0 && o.duration > 0) {
            swprintf(facts, 200, L"%d episodes  -  ~%d min", o.episodes, o.duration);
        } else if (o.episodes > 0) {
            swprintf(facts, 200, L"%d episodes", o.episodes);
        } else {
            swprintf(facts, 200, L"ongoing");
        }
        u.c.text(facts, {tx, hero.y + 44, hero.w - (tx - hero.x) - 16, 20}, accent, f(12.5f));

        if (!o.description.empty()) {
            Font body = f(12);
            body.wrap = true;
            body.ellipsis = true;
            u.c.text(widen(o.description), {tx, hero.y + 68, hero.w - (tx - hero.x) - 16, 66}, dim,
                     body);
        }
        y += 164;
    } else {
        u.c.text(widen(o.torrentName), {kPad, y, avail, 26}, fg, f(17, gfx::Weight::Semibold));
        y += 38;
    }

    // ---- refusal ------------------------------------------------------
    if (o.refused) {
        const Rect warn{kPad, y, avail, 52};
        u.c.fill(warn, gfx::rgb(0x2A1A12), 10);
        u.c.stroke(warn, gfx::rgb(0x6A4A22), 10);
        wchar_t msg[220];
        swprintf(msg, 220,
                 L"Episode %d is not clearly in this torrent, so nothing was played. "
                 L"Pick a file below instead.",
                 o.wanted);
        u.c.text(msg, {warn.x + 14, warn.y + 16, warn.w - 28, 22}, gfx::rgb(0xE0B341), f(12.5f));
        y += 62;
    }

    // ---- episode rows -------------------------------------------------
    int shown = 0;
    for (size_t i = 0; i < o.files.size(); ++i) {
        const auto& file = o.files[i];
        if (file.skipped) continue;
        ++shown;

        const Rect row{kPad, y, avail, 72};
        const int id = 2000 + static_cast<int>(i);
        const bool clicked = u.clickable(id, row);
        const float h = u.hover(id);
        if (h > 0) wantMore = true;

        const bool isTarget = o.target == file.index;
        u.c.fill(row, h > 0.1f ? cardHover : card, kRadius);
        u.c.stroke(row, h > 0.1f || isTarget ? accent : line, kRadius);

        // Thumbnail: the episode still if AniList had one, else the cover.
        const Rect thumb{row.x + 11, row.y + 11, 88, 50};
        const auto meta = o.episodeInfo.find(file.episodeNumber);
        const std::string art =
            (meta != o.episodeInfo.end() && !meta->second.thumb.empty()) ? meta->second.thumb
                                                                        : o.cover;
        u.c.fill(thumb, gfx::rgb(0x0E0E14), 7);
        if (!art.empty()) {
            if (ID2D1Bitmap* b = images::get(art)) u.c.image(b, thumb, 7);
        }
        u.c.gradient(thumb, shade.withAlpha(0.0f), shade.withAlpha(0.7f), 7);
        u.c.text(widen(file.episodeLabel), {thumb.x, thumb.y + 17, thumb.w, 18}, fg,
                 f(12, gfx::Weight::Bold, gfx::Align::Center));

        const float tx = thumb.right() + 14;
        const std::wstring label =
            (meta != o.episodeInfo.end() && !meta->second.title.empty())
                ? widen(meta->second.title)
                : (file.episodeNumber > 0
                       ? L"Episode " + std::to_wstring(file.episodeNumber)
                       : L"Unrecognised");
        u.c.text(label, {tx, row.y + 14, row.w - (tx - row.x) - 130, 20}, fg,
                 f(13.5f, gfx::Weight::Semibold));
        u.c.text(widen(file.name), {tx, row.y + 35, row.w - (tx - row.x) - 130, 18}, dim, f(11.5f));
        u.c.text(sizeText(file.size), {tx, row.y + 52, row.w - (tx - row.x) - 130, 16}, dim,
                 f(11.5f));

        const Rect play{row.right() - 106, row.y + 21, 90, 30};
        u.c.fill(play, h > 0.1f ? accent : gfx::rgb(0x22222E), 8);
        u.c.text(L"Play", {play.x, play.y + 7, play.w, 18},
                 h > 0.1f ? gfx::rgb(0x2A0D18) : fg,
                 f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));

        if (clicked) {
            // Offer to pick up where this episode was left, rather than
            // deciding for them - starting over by accident is worse than one
            // extra click.
            const library::EpisodeProgress p =
                library::get(library::keyFor(o.anilistId, file.episodeNumber, "", -1));
            if (library::worthResuming(p)) {
                st.askingResume = true;
                st.resumeSeconds = p.currentTime;
                st.resumeRemaining = p.remaining();
                st.resumePercent = static_cast<int>(p.percent());
                st.resumeEpisode = file.episodeNumber;
                st.resumeIndex = file.index;
            } else {
                ui::play(file.index, -1);
            }
        }
        y += 72 + 8;
    }

    if (shown == 0) {
        u.c.text(L"No episodes were recognised in this torrent.", {kPad, y, avail, 24}, dim,
                 f(13.5f));
        y += 34;
    }

    // Getting an episode ready takes real time. Cover the list while it
    // happens rather than leaving a page that looks idle.
    {
        const ui::Status s = ui::status();
        if (s.playing && !s.videoActive) {
            u.c.fill(u.c.bounds(), shade.withAlpha(0.66f));
            waitingPanel(u, st, L"Getting the episode ready");
            u.contentHeight = y + st.scroll[static_cast<int>(Screen::Episodes)] + 20;
            return true;
        }
    }

    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Episodes)] + 20;

    // ---- resume prompt, over the top ----------------------------------
    if (st.askingResume) {
        const Rect veil = u.c.bounds();
        u.c.fill(veil, shade.withAlpha(0.62f));

        const Rect box{veil.cx() - 230, veil.cy() - 90, 460, 180};
        u.c.fill(box, gfx::rgb(0x16161F), 14);
        u.c.stroke(box, line, 14);

        u.c.text(L"Resume from " + formatTime(st.resumeSeconds) + L"?",
                 {box.x + 24, box.y + 24, box.w - 48, 26}, fg, f(17, gfx::Weight::Bold));

        wchar_t note[220];
        swprintf(note, 220, L"You stopped %d%% through episode %d, with %s left.",
                 st.resumePercent, st.resumeEpisode, formatTime(st.resumeRemaining).c_str());
        u.c.text(note, {box.x + 24, box.y + 56, box.w - 48, 20}, dim, f(12.5f));

        const Rect go{box.x + 24, box.y + 104, 200, 38};
        const bool goHot = u.clickable(3001, go);
        u.c.fill(go, accent, 9);
        u.c.text(L"Resume from " + formatTime(st.resumeSeconds), {go.x, go.y + 10, go.w, 20},
                 gfx::rgb(0x2A0D18), f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));

        const Rect over{go.right() + 12, box.y + 104, 200, 38};
        const bool overHot = u.clickable(3002, over);
        u.c.fill(over, gfx::rgb(0x22222E), 9);
        u.c.stroke(over, line, 9);
        u.c.text(L"Start from the beginning", {over.x, over.y + 10, over.w, 20}, fg,
                 f(12.5f, gfx::Weight::Medium, gfx::Align::Center));

        if (goHot) {
            st.askingResume = false;
            ui::play(st.resumeIndex, st.resumeSeconds);
        } else if (overHot) {
            st.askingResume = false;
            ui::play(st.resumeIndex, 0);
        }
    }

    return wantMore;
}

}  // namespace tsuzuki::view
