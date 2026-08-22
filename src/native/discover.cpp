// Somewhere to start when you do not already know what you want.
//
// Shelves of what is trending, what is airing this season and what is popular
// overall, each narrowable to a genre. Picking a title runs the search for it,
// so discovery hands straight over to the path that already works rather than
// being a separate way of getting to a file.

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
constexpr float kGap = 14;
constexpr float kPosterW = 148;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

}  // namespace

bool discoverScreen(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    const float avail = w - kPad * 2;
    bool wantMore = false;

    if (async::takeDiscover(st.discovery)) st.discoverLoaded = true;

    // First visit, or the genre just changed.
    if (!st.discoverLoaded && !async::discoverRunning()) {
        async::discover(narrow(st.genre));
    }

    float y = kHeaderH + 22 - st.scroll[static_cast<int>(Screen::Discover)];

    u.c.text(L"Discover", {kPad, y, 400, 30}, fg, f(21, gfx::Weight::Bold));
    y += 40;

    // ---- genre filter ----------------------------------------------------
    {
        static std::vector<std::wstring> chips;
        if (chips.empty()) {
            chips.push_back(L"All");
            for (const auto& g : ui::genreList()) chips.push_back(widen(g));
        }

        float x = kPad;
        for (size_t i = 0; i < chips.size(); ++i) {
            const std::wstring& label = chips[i];
            const float cw = label.size() * 7.0f + 26;
            if (x + cw > kPad + avail) {  // wrap
                x = kPad;
                y += 36;
            }
            const Rect chip{x, y, cw, 30};
            const bool on = (i == 0) ? st.genre.empty() : st.genre == label;
            const bool clicked = u.clickable(5200 + static_cast<int>(i), chip);
            const float h = u.hover(5200 + static_cast<int>(i));
            if (h > 0 && h < 1) wantMore = true;

            u.c.fill(chip, on ? accent : (h > 0.1f ? gfx::rgb(0x2A2A38) : gfx::rgb(0x1A1A24)), 15);
            if (!on) u.c.stroke(chip, line, 15);
            u.c.text(label, {chip.x, chip.y + 6, chip.w, 18},
                     on ? gfx::rgb(0x2A0D18) : (h > 0.1f ? fg : dim),
                     f(12, on ? gfx::Weight::Semibold : gfx::Weight::Regular, gfx::Align::Center));

            if (clicked) {
                st.genre = (i == 0) ? std::wstring() : label;
                st.discoverLoaded = false;
                st.discovery = ui::Discovery{};
                st.scroll[static_cast<int>(Screen::Discover)] = 0;
                st.scrollTarget[static_cast<int>(Screen::Discover)] = 0;
                async::discover(narrow(st.genre));
                return true;
            }
            x += cw + 8;
        }
        y += 46;
    }

    if (async::discoverRunning() && st.discovery.shelves.empty()) {
        u.c.text(L"Asking AniList what is worth watching...", {kPad, y + 30, avail, 24}, dim,
                 f(14, gfx::Weight::Regular, gfx::Align::Center));
        u.contentHeight = 0;
        return true;
    }

    if (st.discovery.shelves.empty()) {
        // A refusal and an empty answer are different things, and saying so is
        // the difference between "wait a moment" and "pick another genre".
        const std::wstring why = st.discovery.error.empty()
                                     ? std::wstring(L"Nothing came back. Try a different genre.")
                                     : widen(st.discovery.error);
        u.c.text(why, {kPad, y + 30, avail, 24}, dim,
                 f(14, gfx::Weight::Regular, gfx::Align::Center));

        if (!st.discovery.error.empty()) {
            const Rect again{u.c.bounds().cx() - 60, y + 66, 120, 34};
            const bool hot = u.clickable(5100, again);
            u.c.fill(again, u.hover(5100) > 0.1f ? accentSoft : accent, 8);
            u.c.text(L"Try again", {again.x, again.y + 8, again.w, 18}, gfx::rgb(0x2A0D18),
                     f(12.5f, gfx::Weight::Semibold, gfx::Align::Center));
            if (hot) {
                st.discoverLoaded = false;
                async::discover(narrow(st.genre));
                return true;
            }
        }
        u.contentHeight = y + 80 + st.scroll[static_cast<int>(Screen::Discover)];
        return false;
    }

    // ---- shelves ---------------------------------------------------------
    int id = 5300;
    for (const auto& shelf : st.discovery.shelves) {
        u.c.text(widen(shelf.title), {kPad, y, avail, 24}, fg, f(15.5f, gfx::Weight::Semibold));
        y += 30;

        // One row per shelf, as many as fit. A shelf that wrapped into a block
        // would read as a grid and lose the sense of "here is a category".
        const int perRow = (std::max)(1, static_cast<int>((avail + kGap) / (kPosterW + kGap)));
        const float posterH = kPosterW * 1.42f;

        for (int i = 0; i < perRow && i < static_cast<int>(shelf.items.size()); ++i) {
            const auto& item = shelf.items[i];
            Rect tile{kPad + i * (kPosterW + kGap), y, kPosterW, posterH};

            const int tileId = id++;
            const bool clicked = u.clickable(tileId, tile);
            const float h = u.hover(tileId);
            if (h > 0 && h < 1) wantMore = true;
            if (h > 0) tile = tile.offset(0, -3 * h);

            u.c.fill(tile, gfx::rgb(0x12121A), 10);
            if (!item.cover.empty()) {
                if (ID2D1Bitmap* b = images::get(item.cover)) u.c.image(b, tile, 10);
            }
            u.c.stroke(tile, h > 0.1f ? accent : line, 10);

            // Score badge, top right, only when AniList has one.
            if (item.score > 0) {
                const Rect badge{tile.right() - 44, tile.y + 8, 36, 20};
                u.c.fill(badge, shade.withAlpha(0.72f), 5);
                wchar_t s[16];
                swprintf(s, 16, L"%d%%", item.score);
                u.c.text(s, {badge.x, badge.y + 2, badge.w, 16},
                         item.score >= 75 ? good : fg,
                         f(11, gfx::Weight::Semibold, gfx::Align::Center));
            }

            u.c.gradient({tile.x, tile.bottom() - 54, tile.w, 54}, shade.withAlpha(0.0f),
                         shade.withAlpha(0.86f), 10);

            Font name = f(11.5f, gfx::Weight::Semibold);
            name.wrap = true;
            u.c.text(widen(item.title), {tile.x + 8, tile.bottom() - 44, tile.w - 16, 34}, fg,
                     name);

            if (clicked) {
                // Straight into the search that already knows how to find a
                // release and pick an episode.
                st.query = widen(item.title);
                st.episodeWanted.clear();
                st.lastAnilistId = item.id;
                st.searchDone = false;
                st.screen = Screen::Results;
                async::search(item.title, 0);
                return true;
            }
        }
        y += posterH + 26;
    }

    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Discover)] + 20;
    return wantMore;
}

}  // namespace tsuzuki::view
