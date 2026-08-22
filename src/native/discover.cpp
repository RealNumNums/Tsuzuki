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

struct Grid {
    int cols;
    float itemW;
};
Grid gridFor(float avail, float minW) {
    int cols = static_cast<int>((avail + kGap) / (minW + kGap));
    if (cols < 1) cols = 1;
    return {cols, (avail - kGap * (cols - 1)) / cols};
}

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

// One poster. Returns true when it was clicked.
bool poster(Ui& u, State& st, int id, Rect tile, const ui::DiscoverItem& item,
            bool& wantMore) {
    const bool clicked = u.clickable(id, tile);
    const float h = u.hover(id);
    if (h > 0 && h < 1) wantMore = true;
    if (h > 0) tile = tile.offset(0, -3 * h);

    u.c.fill(tile, gfx::rgb(0x12121A), 10);
    if (!item.cover.empty()) {
        if (ID2D1Bitmap* b = images::get(item.cover)) u.c.image(b, tile, 10);
    }
    u.c.stroke(tile, h > 0.1f ? accent : line, 10);

    if (item.score > 0) {
        const Rect badge{tile.right() - 44, tile.y + 8, 36, 20};
        u.c.fill(badge, shade.withAlpha(0.72f), 5);
        wchar_t sc[16];
        swprintf(sc, 16, L"%d%%", item.score);
        u.c.text(sc, {badge.x, badge.y + 2, badge.w, 16}, item.score >= 75 ? good : fg,
                 f(11, gfx::Weight::Semibold, gfx::Align::Center));
    }

    u.c.gradient({tile.x, tile.bottom() - 54, tile.w, 54}, shade.withAlpha(0.0f),
                 shade.withAlpha(0.86f), 10);

    Font name = f(11.5f, gfx::Weight::Semibold);
    name.wrap = true;
    u.c.text(widen(item.title), {tile.x + 8, tile.bottom() - 44, tile.w - 16, 34}, fg, name);
    return clicked;
}

// Opening a title hands over to the search that already works.
void openTitle(State& st, const ui::DiscoverItem& item) {
    st.query = widen(item.title);
    st.episodeWanted.clear();
    st.lastAnilistId = item.id;
    st.searchDone = false;
    st.screen = Screen::Results;
    async::search(item.title, 0);
}

}  // namespace

// One category, in full, fetching another page as it is scrolled.
bool shelfScreen(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    const float avail = w - kPad * 2;
    bool wantMore = false;

    {
        ui::MorePage page;
        if (async::takeMore(page)) {
            if (page.items.empty()) {
                st.openShelfExhausted = true;  // nothing further to ask for
            } else {
                st.openShelfItems.insert(st.openShelfItems.end(), page.items.begin(),
                                         page.items.end());
            }
        }
    }

    float y = kHeaderH + 22 - st.scroll[static_cast<int>(Screen::Shelf)];

    const Rect back{kPad, y, 150, 30};
    const bool backHot = u.clickable(5000, back);
    u.c.fill(back, u.hover(5000) > 0.1f ? gfx::rgb(0x22222E) : gfx::rgb(0x16161F), 8);
    u.c.stroke(back, line, 8);
    u.c.text(L"< Back to Discover", {back.x, back.y + 6, back.w, 18}, dim,
             f(12, gfx::Weight::Medium, gfx::Align::Center));
    if (backHot) {
        st.screen = Screen::Discover;
        return true;
    }
    y += 44;

    u.c.text(st.openShelfTitle, {kPad, y, avail, 30}, fg, f(21, gfx::Weight::Bold));
    y += 42;

    const Grid g = gridFor(avail, kPosterW);
    const float posterH = g.itemW * 1.42f;
    for (size_t i = 0; i < st.openShelfItems.size(); ++i) {
        const int col = static_cast<int>(i) % g.cols;
        const int rowIdx = static_cast<int>(i) / g.cols;
        const Rect tile{kPad + col * (g.itemW + kGap), y + rowIdx * (posterH + 30), g.itemW,
                        posterH};
        if (poster(u, st, 6000 + static_cast<int>(i), tile, st.openShelfItems[i], wantMore)) {
            openTitle(st, st.openShelfItems[i]);
            return true;
        }
    }
    const int rows = (static_cast<int>(st.openShelfItems.size()) + g.cols - 1) / g.cols;
    y += rows * (posterH + 30) + 10;

    if (async::moreRunning()) {
        u.c.text(L"Loading more...", {kPad, y, avail, 22}, dim,
                 f(13, gfx::Weight::Regular, gfx::Align::Center));
        y += 40;
        wantMore = true;
    } else if (!st.openShelfExhausted) {
        const Rect more{u.c.bounds().cx() - 90, y, 180, 38};
        const bool hot = u.clickable(5001, more);
        u.c.fill(more, u.hover(5001) > 0.1f ? accentSoft : accent, 9);
        u.c.text(L"Load more", {more.x, more.y + 10, more.w, 20}, gfx::rgb(0x2A0D18),
                 f(13, gfx::Weight::Semibold, gfx::Align::Center));
        if (hot) {
            ++st.openShelfPage;
            async::more(st.openShelf, narrow(st.genre), st.openShelfPage);
        }
        y += 52;
    } else {
        u.c.text(L"That is everything AniList has for this one.",
                 {kPad, y, avail, 22}, dim, f(12, gfx::Weight::Regular, gfx::Align::Center));
        y += 40;
    }

    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Shelf)] + 20;
    return wantMore;
}

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

        // A shelf shows one row; this is the way into the rest of it.
        {
            const Rect all{kPad + avail - 80, y - 3, 80, 24};
            const bool hot = u.clickable(id++, all);
            u.c.text(L"See all >", {all.x, all.y + 3, all.w, 18},
                     u.hover(id - 1) > 0.1f ? accent : dim,
                     f(12, gfx::Weight::Medium, gfx::Align::Right));
            if (hot) {
                st.openShelf = shelf.key;
                st.openShelfTitle = widen(shelf.title);
                st.openShelfItems = shelf.items;  // page one, already fetched
                st.openShelfPage = 1;
                st.openShelfExhausted = false;
                st.scroll[static_cast<int>(Screen::Shelf)] = 0;
                st.scrollTarget[static_cast<int>(Screen::Shelf)] = 0;
                st.screen = Screen::Shelf;
                return true;
            }
        }
        y += 30;

        // One row per shelf, as many as fit. A shelf that wrapped into a block
        // would read as a grid and lose the sense of "here is a category".
        const int perRow = (std::max)(1, static_cast<int>((avail + kGap) / (kPosterW + kGap)));
        const float posterH = kPosterW * 1.42f;

        for (int i = 0; i < perRow && i < static_cast<int>(shelf.items.size()); ++i) {
            const Rect tile{kPad + i * (kPosterW + kGap), y, kPosterW, posterH};
            if (poster(u, st, id++, tile, shelf.items[i], wantMore)) {
                openTitle(st, shelf.items[i]);
                return true;
            }
        }
        y += posterH + 26;
    }

    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Discover)] + 20;
    return wantMore;
}

}  // namespace tsuzuki::view
