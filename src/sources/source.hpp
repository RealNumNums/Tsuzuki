#pragma once

// Native torrent sources - milestone 2.
//
// Hayase runs these as JavaScript extensions in a Web Worker, fetched from a
// repo index. We implement them natively instead, so there is no JS engine in
// the binary. Ported from the set in:
//   https://github.com/resirch/hayase-extensions  (index.json)
//
//   Nyaa        https://nyaa.si            RSS      accuracy: medium
//   SeaDex      https://releases.moe       JSON     accuracy: high
//   AnimeTosho  https://feed.animetosho.org JSON    accuracy: high
//   nekoBT      https://nekobt.to          -        accuracy: medium
//   SubsPlease  https://subsplease.org     JSON     accuracy: medium
//
// Trade-off worth remembering: JS extensions can be hot-fixed when a site
// changes its markup; native ones need a rebuild. So keep endpoints and field
// mappings in config where possible, and put as little parsing logic in C++ as
// each source allows. Most of these expose real APIs, so this is cheap - Nyaa
// is the only one needing feed parsing, and it is RSS rather than HTML.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tsuzuki::sources {

enum class Accuracy { Low, Medium, High };

struct Query {
    std::string title;              // series title to search for
    std::vector<std::string> altTitles;
    std::optional<int> episode;
    std::optional<int> season;
    std::optional<int> resolution;  // 1080, 720, ...
    std::optional<int> anilistId;   // SeaDex is keyed by AniList ID, not title
    bool batch = false;             // looking for a season pack
    bool exclusive = false;         // only trusted/best releases
};

struct Result {
    std::string title;      // full release name, as published
    std::string infoHash;   // primary dedupe key
    std::string magnet;
    std::string link;
    std::int64_t size = 0;
    int seeders = 0;
    int leechers = 0;
    std::int64_t downloads = 0;
    std::string date;       // ISO 8601
    Accuracy accuracy = Accuracy::Medium;
    std::string sourceId;   // which source produced this
    // Curated "this is the best encode" pick (SeaDex). These carry no seeder
    // counts, so without an explicit flag they sort to the bottom - which
    // would waste the one source that actually knows which release is good.
    bool curatedBest = false;
};

class Source {
public:
    virtual ~Source() = default;
    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual Accuracy accuracy() const = 0;
    virtual std::vector<Result> search(const Query& q) = 0;
};

// Runs every enabled source, merges, and dedupes by infoHash - keeping the
// longest title and the highest accuracy seen for each hash, the way Hayase
// merges its extension results.
std::vector<Result> searchAll(const std::vector<std::shared_ptr<Source>>& sources,
                              const Query& q);

std::shared_ptr<Source> makeNyaa();
std::shared_ptr<Source> makeSeaDex();
std::shared_ptr<Source> makeAnimeTosho();
std::shared_ptr<Source> makeNekoBT();
std::shared_ptr<Source> makeSubsPlease();

}  // namespace tsuzuki::sources
