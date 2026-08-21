#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tsuzuki {

// An episode reference parsed out of a filename. Kept as integers, always.
//
// This type exists because of a specific bug in Hayase's resolver
// (interface/src/lib/components/ui/player/resolver.ts): its episode field is
// typed `string | number | undefined`, and multi-episode files get assigned a
// range *string* like "1 ~ 12". It then matches with `===` against a number,
// so those files never compare equal, the lookup silently falls through, and
// the player hands you a different episode. Never store an episode as text.
struct Episode {
    int from = 0;
    int to = 0;  // == from for a single episode
    bool valid = false;

    bool isRange() const { return valid && to > from; }
    bool contains(int ep) const { return valid && ep >= from && ep <= to; }
    bool isExactly(int ep) const { return valid && !isRange() && from == ep; }
};

struct ScannedFile {
    int index = 0;  // index within the torrent
    std::string path;
    std::string name;
    std::int64_t size = 0;

    std::string title;           // anitomy: anime_title
    std::optional<int> season;   // anitomy: anime_season
    Episode episode;
    std::string type;            // anitomy: anime_type (OP / ED / NCOP / ...)

    bool excluded = false;       // not a real episode (opening, preview, ...)
    std::string excludeReason;
};

// Parse every file through Anitomy. Non-video files are dropped; openings,
// endings and previews are kept but flagged `excluded` so they stay visible
// in the table rather than vanishing silently.
std::vector<ScannedFile> scanFiles(
    const std::vector<std::pair<std::string, std::int64_t>>& files);

// Find the file for `wanted`.
//
// Returns nullptr when there is no confident match. That is the whole point:
// this function never substitutes a different episode, never falls back to
// "episode 1", and never falls back to "first file". A miss is reported to
// the user, who then picks from the table.
const ScannedFile* selectEpisode(const std::vector<ScannedFile>& files, int wanted);

void printTable(const std::vector<ScannedFile>& files);

}  // namespace tsuzuki
