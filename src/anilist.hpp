#pragma once

#include <string>
#include <vector>

namespace tsuzuki::anilist {

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
    int episodes = 0;
    int duration = 0;  // minutes
    std::string title;
    std::string description;  // plain text
    std::string coverImage;
    std::string bannerImage;
    std::string color;  // dominant cover colour, e.g. "#e4bb5d"
    std::vector<EpisodeInfo> episodeInfo;
};

bool details(int id, Details& out);

}  // namespace tsuzuki::anilist
