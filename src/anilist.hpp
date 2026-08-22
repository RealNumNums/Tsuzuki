#pragma once

#include <string>
#include <vector>

#include "http.hpp"

namespace tsuzuki::anilist {

// ---- the gate --------------------------------------------------------
//
// Every AniList request in the program goes through post(). One budget,
// one place that knows how much of it is left, one place that waits.
//
// The alternative - each caller minding its own manners - is what was here
// before, and it does not work: nobody can see the whole picture, the
// budget is only discovered by exceeding it, and the recovery is whatever
// each caller happens to do.

// Seconds until requests will be sent again. Zero when nothing is holding
// them back, so the interface can say "waiting 34s" rather than "failed".
long long pausedFor();

// Remaining requests in the current window as AniList last reported it,
// or -1 before it has said.
int budgetLeft();

// The one way to talk to AniList. Both this file and the account layer use it.
http::Response post(const std::string& body, const std::string& bearer);

struct Media {
    int id = 0;          // AniList ID - also the key SeaDex indexes by
    int episodes = 0;    // 0 when unknown or still airing
    int year = 0;
    std::string format;  // TV / MOVIE / OVA / ONA / SPECIAL
    std::string romaji;
    std::string english;
    std::string native;
    std::string preferred;
    std::vector<std::string> synonyms;
    bool isAdult = false;
};

// Title search against AniList's public GraphQL API. Returns up to 5 matches,
// best first. Empty on any failure - callers fall back to a raw title search.
std::vector<Media> search(const std::string& text);

struct EpisodeInfo {
    int number = 0;
    std::string title;
    std::string thumbnail;
};

// Artwork and blurb for the show, plus whatever per-episode data AniList has.
//
// streamingEpisodes is frequently empty - it is populated from licensed
// streaming sites, so older or unlicensed shows have none at all. The UI
// therefore treats episode titles and thumbnails as a bonus and falls back to
// the cover image, rather than depending on them.
struct Details {
    int id = 0;
    // MyAnimeList id for the same show. AniList publishes it, and it is the
    // bridge every other tracker understands - Simkl and Kitsu both accept a
    // MAL id, and MAL obviously does. Without it there is no way to say "this
    // show" to anyone but AniList.
    int idMal = 0;
    int episodes = 0;
    int duration = 0;  // minutes
    std::string title;    // romaji, or english when there is no romaji
    std::string english;  // empty when AniList has no english title
    std::string native;
    std::string description;  // plain text
    std::string coverImage;
    std::string bannerImage;
    std::string color;  // dominant cover colour, e.g. "#e4bb5d"
    std::vector<EpisodeInfo> episodeInfo;
};

bool details(int id, Details& out);

// ---- discovery ------------------------------------------------------

struct BrowseFilters {
    std::string search;
    std::string genre;
    std::string season;   // WINTER / SPRING / SUMMER / FALL
    std::string format;   // TV / MOVIE / OVA / ONA / SPECIAL
    std::string status;   // RELEASING / FINISHED / NOT_YET_RELEASED
    std::string sort = "POPULARITY_DESC";
    int year = 0;
    int page = 1;
    bool allowAdult = false;
};

struct BrowseItem {
    int id = 0;
    int episodes = 0;
    int year = 0;
    int score = 0;        // averageScore, 0 when unrated
    std::string title;
    std::string cover;
    std::string color;
    std::string format;
    std::string status;
    std::vector<std::string> genres;
};

std::vector<BrowseItem> browse(const BrowseFilters& f);

// All the discovery shelves in one request.
//
// GraphQL lets several aliased queries share a request, so four shelves cost
// one round trip instead of four. That matters: AniList rate-limits, and
// four-at-once was enough to be refused - which arrived as four empty
// shelves and looked like "there is nothing to show".
struct DiscoverShelf {
    std::string title;
    std::vector<BrowseItem> items;
};

// `error` is set when AniList refused, so a refusal can be told apart from
// an genuinely empty answer.
std::vector<DiscoverShelf> discoverShelves(const std::string& genre, bool allowAdult,
                                           std::string* error);

// Episodes airing in the next `days` days, soonest first.
struct AiringItem {
    int mediaId = 0;
    int episode = 0;
    long long airingAt = 0;   // unix seconds
    std::string title;
    std::string cover;
    std::string color;
};

std::vector<AiringItem> airing(int days = 7);

// The genres AniList knows about, for the filter menu.
std::vector<std::string> genres();

}  // namespace tsuzuki::anilist
