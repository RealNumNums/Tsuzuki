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
};

// Title search against AniList's public GraphQL API. Returns up to 5 matches,
// best first. Empty on any failure - callers fall back to a raw title search.
std::vector<Media> search(const std::string& text);

}  // namespace tsuzuki::anilist
