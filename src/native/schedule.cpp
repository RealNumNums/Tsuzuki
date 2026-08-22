// The airing calendar.
//
// A month at a time, because "what is on this week" is a question people ask
// of a calendar shape rather than a list. Each cell holds whatever airs that
// day, and the whole month is one request.
//
// This existed in the old web interface and was lost in the move to Direct2D;
// this is it back, as a calendar rather than the flat list it used to be.

#include "../ui.hpp"
#include "async.hpp"
#include "images.hpp"
#include "view.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>

namespace tsuzuki::view {
namespace {

using namespace gfx::theme;

constexpr float kPad = 26;
constexpr float kHeaderH = 62;

Font f(float size, gfx::Weight w = gfx::Weight::Regular, gfx::Align a = gfx::Align::Left) {
    Font font;
    font.size = size;
    font.weight = w;
    font.align = a;
    return font;
}

const wchar_t* kMonths[] = {L"January", L"February", L"March",     L"April",
                            L"May",     L"June",     L"July",      L"August",
                            L"September", L"October", L"November", L"December"};

std::tm localFrom(long long unixSeconds) {
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

// Midnight local time on the first of a month, as unix seconds.
long long startOfMonth(int year, int month0) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month0;
    tm.tm_mday = 1;
    tm.tm_isdst = -1;
    return static_cast<long long>(std::mktime(&tm));
}

int daysInMonth(int year, int month0) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month0 == 1) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        return leap ? 29 : 28;
    }
    return days[month0];
}

// Monday-first, which is how a season's week actually reads.
int mondayIndex(int year, int month0) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month0;
    tm.tm_mday = 1;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    const std::tm local = localFrom(static_cast<long long>(t));
    return (local.tm_wday + 6) % 7;  // tm_wday is Sunday-first
}

}  // namespace

bool scheduleScreen(Ui& u, State& st) {
    const float w = u.c.bounds().w;
    const float avail = w - kPad * 2;
    bool wantMore = false;

    // First visit: this month.
    if (st.calYear == 0) {
        const std::tm now = localFrom(static_cast<long long>(std::time(nullptr)));
        st.calYear = now.tm_year + 1900;
        st.calMonth = now.tm_mon;
    }
    // The mode is part of the key: your list and everything airing are
    // different answers for the same month.
    const int key = (st.calYear * 12 + st.calMonth) * 2 + (st.calMineOnly ? 1 : 0);

    // Pages arrive one at a time and belong to whichever month asked for them,
    // which is not necessarily the one on screen by the time they land.
    if (st.calPendingKey >= 0) {
        std::vector<ui::AiringEntry>& slot = st.calCache[st.calPendingKey];
        const size_t before = slot.size();
        async::takeSchedule(slot);
        const bool done = !async::scheduleRunning();

        // Show each page as it arrives rather than holding the month back for
        // the slowest request. On the last one adopt the answer even when it is
        // empty, because "nothing of yours airs this month" is an answer.
        if (st.calPendingKey == key && (slot.size() != before || done)) {
            st.airing = slot;
            st.calShownKey = key;
        }
        if (done) {
            st.calDone.insert(st.calPendingKey);
            st.calPendingKey = -1;
        }
    }

    if (st.calShownKey != key && st.calPendingKey != key) {
        const auto hit = st.calCache.find(key);
        if (hit != st.calCache.end() && st.calDone.count(key)) {
            st.airing = hit->second;
            st.calShownKey = key;
        } else if (!async::scheduleRunning()) {
            st.airing.clear();
            st.calCache.erase(key);  // a half-filled month is worth redoing
            st.calPendingKey = key;
            const long long from = startOfMonth(st.calYear, st.calMonth);
            const long long to =
                from + static_cast<long long>(daysInMonth(st.calYear, st.calMonth)) * 86400;
            async::schedule(from, to, st.calMineOnly);
        }
    }
    const bool stillFilling = st.calPendingKey == key && async::scheduleRunning();

    float y = kHeaderH + 20 - st.scroll[static_cast<int>(Screen::Schedule)];

    // ---- month header ----------------------------------------------------
    {
        const Rect prev{kPad, y, 38, 32};
        const Rect next{kPad + avail - 38, y, 38, 32};
        const bool prevHot = u.clickable(9200, prev);
        const bool nextHot = u.clickable(9201, next);

        for (const auto& [r, glyph, hot] :
             {std::tuple<Rect, const wchar_t*, bool>{prev, L"<", prevHot},
              std::tuple<Rect, const wchar_t*, bool>{next, L">", nextHot}}) {
            u.c.fill(r, gfx::rgb(0x1A1A24), 8);
            u.c.stroke(r, line, 8);
            u.c.text(glyph, {r.x, r.y + 7, r.w, 18}, fg,
                     f(13, gfx::Weight::Bold, gfx::Align::Center));
        }

        wchar_t heading[64];
        swprintf(heading, 64, L"%s %d", kMonths[st.calMonth], st.calYear);
        u.c.text(heading, {kPad, y + 4, avail, 26}, fg,
                 f(18, gfx::Weight::Bold, gfx::Align::Center));

        if (prevHot || nextHot) {
            st.calMonth += nextHot ? 1 : -1;
            if (st.calMonth > 11) {
                st.calMonth = 0;
                ++st.calYear;
            } else if (st.calMonth < 0) {
                st.calMonth = 11;
                --st.calYear;
            }
            return true;
        }
        y += 40;
    }

    // ---- my-list filter --------------------------------------------------
    {
        const Rect pill{u.c.bounds().cx() - 52, y, 104, 26};
        if (u.clickable(9202, pill)) {
            st.calMineOnly = !st.calMineOnly;
            return true;  // a different key, so the next frame fetches or reuses
        }
        u.c.fill(pill, st.calMineOnly ? accent : gfx::rgb(0x1A1A24), 13);
        if (!st.calMineOnly) u.c.stroke(pill, line, 13);
        u.c.text(L"My list", {pill.x, pill.y + 5, pill.w, 18},
                 st.calMineOnly ? gfx::rgb(0x2A0D18) : dim,
                 f(12, gfx::Weight::Medium, gfx::Align::Center));

        if (stillFilling && st.calShownKey == key) {
            u.c.text(L"still filling in the rest of the month...",
                     {pill.right() + 14, pill.y + 5, 260, 18}, dim, f(11));
        }
        y += 38;
    }

    if (st.calShownKey != key) {
        u.c.text(L"Asking AniList what is airing...", {kPad, y + 30, avail, 24}, dim,
                 f(14, gfx::Weight::Regular, gfx::Align::Center));
        u.contentHeight = 0;
        return true;
    }

    if (st.airing.empty()) {
        u.c.text(st.calMineOnly
                     ? L"Nothing from your AniList airs this month. Turn off My list to see "
                       L"everything."
                     : L"AniList has no airings for this month.",
                 {kPad, y + 30, avail, 24}, dim,
                 f(13, gfx::Weight::Regular, gfx::Align::Center));
        u.contentHeight = 0;
        return true;
    }

    // ---- weekday strip ---------------------------------------------------
    const float colW = avail / 7.0f;
    {
        static const wchar_t* names[] = {L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"};
        for (int i = 0; i < 7; ++i) {
            u.c.text(names[i], {kPad + i * colW, y, colW, 18}, dim,
                     f(11.5f, gfx::Weight::Semibold, gfx::Align::Center));
        }
        y += 24;
        u.c.fill({kPad, y - 4, avail, 1}, line);
    }

    // ---- the grid --------------------------------------------------------
    const int firstCol = mondayIndex(st.calYear, st.calMonth);
    const int total = daysInMonth(st.calYear, st.calMonth);
    const int rows = (firstCol + total + 6) / 7;
    const float cellH = 124;

    const std::tm today = localFrom(static_cast<long long>(std::time(nullptr)));
    const bool thisMonth =
        today.tm_year + 1900 == st.calYear && today.tm_mon == st.calMonth;

    for (int cell = 0; cell < rows * 7; ++cell) {
        const int day = cell - firstCol + 1;
        const int col = cell % 7;
        const int row = cell / 7;
        const Rect box{kPad + col * colW, y + row * cellH, colW, cellH};

        u.c.stroke(box, gfx::rgb(0x1C1C26), 0);
        if (day < 1 || day > total) continue;

        const bool isToday = thisMonth && day == today.tm_mday;
        if (isToday) {
            u.c.fill({box.x + 6, box.y + 6, 22, 20}, accent, 10);
        }
        wchar_t num[8];
        swprintf(num, 8, L"%d", day);
        u.c.text(num, {box.x + 6, box.y + 7, 22, 18}, isToday ? gfx::rgb(0x2A0D18) : dim,
                 f(11.5f, gfx::Weight::Semibold, gfx::Align::Center));

        // What airs on this day.
        float itemY = box.y + 30;
        for (const auto& a : st.airing) {
            if (st.calMineOnly && !a.onMyList) continue;  // belt and braces
            const std::tm when = localFrom(a.airingAt);
            if (when.tm_mday != day || when.tm_mon != st.calMonth) continue;
            if (itemY + 18 > box.bottom() - 4) break;  // out of room in this cell

            const Rect strip{box.x + 5, itemY, box.w - 10, 17};
            const int id = 9300 + cell * 8 + static_cast<int>((itemY - box.y) / 18);
            const bool clicked = u.clickable(id, strip);
            const float hv = u.hover(id);
            if (hv > 0 && hv < 1) wantMore = true;
            if (hv > 0.1f) u.c.fill(strip, gfx::rgb(0x22222E), 4);

            u.c.fill({strip.x + 2, strip.y + 6, 5, 5}, a.onMyList ? accent : gfx::rgb(0x4A4A5A),
                     2.5f);

            wchar_t time[16];
            swprintf(time, 16, L"%02d:%02d", when.tm_hour, when.tm_min);
            u.c.text(widen(a.title), {strip.x + 12, strip.y, strip.w - 56, 16},
                     a.onMyList ? fg : dim, f(10.5f));
            u.c.text(time, {strip.right() - 42, strip.y, 40, 16}, dim,
                     f(10, gfx::Weight::Regular, gfx::Align::Right));

            if (clicked) {
                st.query = widen(a.title);
                wchar_t ep[16];
                swprintf(ep, 16, L"%d", a.episode);
                st.episodeWanted = ep;
                st.lastAnilistId = a.mediaId;
                st.searchDone = false;
                st.screen = Screen::Results;
                async::search(a.title, 0);
                return true;
            }
            itemY += 18;
        }
    }

    y += rows * cellH + 20;
    u.contentHeight = y + st.scroll[static_cast<int>(Screen::Schedule)];
    return wantMore;
}

}  // namespace tsuzuki::view
