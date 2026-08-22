#include "anilist.hpp"
#include <algorithm>
#include <thread>
#include <mutex>
#include <chrono>

#include <nlohmann/json.hpp>

#include <cctype>
#include <ctime>

#include "http.hpp"

namespace tsuzuki::anilist {



using nlohmann::json;

namespace {

constexpr const char* kEndpoint = "https://graphql.anilist.co";

namespace {

std::mutex g_gate;
std::chrono::steady_clock::time_point g_nextAllowed{};
std::chrono::steady_clock::time_point g_lastSent{};
int g_remaining = -1;

// AniList publishes 90 requests a minute and has been observed running well
// below that, so the pacing is driven by what it reports rather than by the
// documented figure. These are only the floor and the panic threshold.
constexpr int kSlowDownBelow = 20;   // start spacing requests out
constexpr int kCrawlBelow = 5;       // one every two seconds

}  // namespace




constexpr const char* kSearchQuery = R"(
query ($search: String) {
  Page(page: 1, perPage: 5) {
    media(search: $search, type: ANIME, sort: SEARCH_MATCH) {
      id
      episodes
      format
      seasonYear
      isAdult
      title { romaji english native }
      synonyms
    }
  }
}
)";

std::string pick(const json& titles, const char* key) {
    if (!titles.contains(key) || titles[key].is_null()) return {};
    return titles[key].get<std::string>();
}

}  // namespace

long long pausedFor() {
    std::lock_guard<std::mutex> lock(g_gate);
    const auto now = std::chrono::steady_clock::now();
    if (g_nextAllowed <= now) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(g_nextAllowed - now).count() + 1;
}

int budgetLeft() {
    std::lock_guard<std::mutex> lock(g_gate);
    return g_remaining;
}

http::Response post(const std::string& body, const std::string& bearer) {
    // Wait out any hold, in short sleeps so shutdown is not delayed by a
    // thread parked on a minute-long timer.
    for (;;) {
        std::chrono::steady_clock::duration wait{};
        {
            std::lock_guard<std::mutex> lock(g_gate);
            const auto now = std::chrono::steady_clock::now();
            if (now >= g_nextAllowed) {
                // Claim this slot before releasing the lock, so two threads
                // cannot both decide it is their turn.
                g_lastSent = now;
                break;
            }
            wait = g_nextAllowed - now;
        }
        std::this_thread::sleep_for(
            (std::min)(std::chrono::duration_cast<std::chrono::milliseconds>(wait),
                       std::chrono::milliseconds(200)));
    }

    const http::Response res = http::postJson(kEndpoint, body, 20, bearer);

    std::lock_guard<std::mutex> lock(g_gate);
    if (res.rateLimitRemaining >= 0) g_remaining = res.rateLimitRemaining;

    const auto now = std::chrono::steady_clock::now();
    if (res.status == 429) {
        // Retry-After is authoritative; a minute is the fallback when the
        // header is missing. Everything waits, not just this caller - the
        // limit is per address, so one thread pressing on would keep the
        // whole program refused.
        const long long secs = res.retryAfterSeconds > 0 ? res.retryAfterSeconds : 60;
        g_nextAllowed = now + std::chrono::seconds(secs);
        g_remaining = 0;
    } else if (g_remaining >= 0 && g_remaining < kCrawlBelow) {
        g_nextAllowed = now + std::chrono::seconds(2);
    } else if (g_remaining >= 0 && g_remaining < kSlowDownBelow) {
        g_nextAllowed = now + std::chrono::milliseconds(600);
    }
    return res;
}

std::vector<Media> search(const std::string& text) {
    std::vector<Media> out;

    json body;
    body["query"] = kSearchQuery;
    body["variables"]["search"] = text;

    const auto res = post(body.dump(), "");
    if (!res.ok) return out;

    json j;
    try {
        j = json::parse(res.body);
    } catch (const std::exception&) {
        return out;
    }

    if (!j.contains("data") || j["data"].is_null()) return out;
    const auto& page = j["data"]["Page"];
    if (!page.contains("media")) return out;

    for (const auto& m : page["media"]) {
        Media media;
        media.id = m.value("id", 0);
        media.episodes = m.contains("episodes") && !m["episodes"].is_null()
                             ? m["episodes"].get<int>()
                             : 0;
        media.format = m.value("format", "");
        media.isAdult = m.value("isAdult", false);
        media.year = m.contains("seasonYear") && !m["seasonYear"].is_null()
                         ? m["seasonYear"].get<int>()
                         : 0;

        const auto& t = m["title"];
        media.romaji = pick(t, "romaji");
        media.english = pick(t, "english");
        media.native = pick(t, "native");

        for (const auto& s : m.value("synonyms", json::array())) {
            if (s.is_string()) media.synonyms.push_back(s.get<std::string>());
        }

        media.preferred = !media.romaji.empty() ? media.romaji
                          : !media.english.empty() ? media.english
                                                   : media.native;
        if (media.id) out.push_back(std::move(media));
    }
    return out;
}

namespace {

constexpr const char* kDetailsQuery = R"(
query ($id: Int) {
  Media(id: $id, type: ANIME) {
    id
    episodes
    duration
    title { romaji english native }
    description(asHtml: false)
    coverImage { extraLarge large color }
    bannerImage
    streamingEpisodes { title thumbnail }
  }
}
)";

// AniList descriptions carry a little HTML even with asHtml:false.
std::string stripTags(std::string s) {
    std::string out;
    bool inTag = false;
    for (const char c : s) {
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) out.push_back(c);
    }
    // collapse the <br> gaps that leaves behind
    std::string tidy;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == '\n' && !tidy.empty() && tidy.back() == '\n') continue;
        tidy.push_back(out[i]);
    }
    return tidy;
}

// "Episode 3 - The Title" / "E3 - Title" -> number 3. Returns 0 if absent.
int episodeNumberFrom(const std::string& title) {
    for (std::size_t i = 0; i < title.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(title[i]))) {
            int n = 0;
            while (i < title.size() && std::isdigit(static_cast<unsigned char>(title[i]))) {
                n = n * 10 + (title[i] - '0');
                ++i;
            }
            return n;
        }
    }
    return 0;
}

}  // namespace

bool details(int id, Details& out) {
    json body;
    body["query"] = kDetailsQuery;
    body["variables"]["id"] = id;

    const auto res = post(body.dump(), "");
    if (!res.ok) return false;

    json j;
    try {
        j = json::parse(res.body);
    } catch (const std::exception&) {
        return false;
    }
    if (!j.contains("data") || j["data"].is_null() || j["data"]["Media"].is_null()) {
        return false;
    }

    const auto& m = j["data"]["Media"];
    out.id = m.value("id", 0);
    out.idMal = m.contains("idMal") && !m["idMal"].is_null() ? m["idMal"].get<int>() : 0;
    out.episodes = m.contains("episodes") && !m["episodes"].is_null()
                       ? m["episodes"].get<int>() : 0;
    out.duration = m.contains("duration") && !m["duration"].is_null()
                       ? m["duration"].get<int>() : 0;

    const auto& t = m["title"];
    out.title = pick(t, "romaji");
    out.english = pick(t, "english");
    out.native = pick(t, "native");
    if (out.title.empty()) out.title = out.english;

    if (m.contains("description") && m["description"].is_string()) {
        out.description = stripTags(m["description"].get<std::string>());
    }
    if (m.contains("coverImage") && m["coverImage"].is_object()) {
        const auto& c = m["coverImage"];
        out.coverImage = pick(c, "extraLarge");
        if (out.coverImage.empty()) out.coverImage = pick(c, "large");
        out.color = pick(c, "color");
    }
    if (m.contains("bannerImage") && m["bannerImage"].is_string()) {
        out.bannerImage = m["bannerImage"].get<std::string>();
    }

    int index = 0;
    for (const auto& se : m.value("streamingEpisodes", json::array())) {
        EpisodeInfo info;
        info.title = se.value("title", "");
        info.thumbnail = se.value("thumbnail", "");
        info.number = episodeNumberFrom(info.title);
        if (info.number == 0) info.number = ++index;  // fall back to order
        out.episodeInfo.push_back(std::move(info));
    }
    return true;
}

namespace {

constexpr const char* kAiringQuery = R"(
query ($from: Int, $to: Int, $page: Int) {
  Page(page: $page, perPage: 50) {
    pageInfo { hasNextPage }
    airingSchedules(airingAt_greater: $from, airingAt_lesser: $to, sort: TIME) {
      episode
      airingAt
      media {
        id
        title { romaji english }
        coverImage { large color }
        isAdult
      }
    }
  }
}
)";

int intOr(const json& v, int fallback) {
    return v.is_number_integer() ? v.get<int>() : fallback;
}

}  // namespace

namespace {

// Shared by the shelf query and browse().
BrowseItem parseBrowseItem(const json& m) {
    BrowseItem b;
    b.id = intOr(m.contains("id") ? m["id"] : json(), 0);
    if (!b.id) return b;
    b.episodes = intOr(m.contains("episodes") ? m["episodes"] : json(), 0);
    b.year = intOr(m.contains("seasonYear") ? m["seasonYear"] : json(), 0);
    b.score = intOr(m.contains("averageScore") ? m["averageScore"] : json(), 0);
    b.format = pick(m, "format");
    b.status = pick(m, "status");

    const json t = m.value("title", json::object());
    b.title = pick(t, "romaji");
    if (b.title.empty()) b.title = pick(t, "english");

    const json cover = m.value("coverImage", json::object());
    b.cover = pick(cover, "large");
    b.color = pick(cover, "color");
    return b;
}

}  // namespace

namespace {

// Every shelf in one table. The batch query and the "see all" paging both
// read from here, so a shelf cannot mean one thing on the front page and
// something else when opened.
struct ShelfSpec {
    const char* key;
    const char* title;
    std::string args;  // extra arguments to media(), beyond type and genre
};

std::vector<ShelfSpec> shelfSpecs() {
    int year = 0, month = 0;
    {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        year = tm.tm_year + 1900;
        month = tm.tm_mon + 1;
    }
    const char* season =
        month <= 3 ? "WINTER" : month <= 6 ? "SPRING" : month <= 9 ? "SUMMER" : "FALL";

    // The season after this one, rolling into January where it has to.
    const char* nextSeason = month <= 3    ? "SPRING"
                             : month <= 6  ? "SUMMER"
                             : month <= 9  ? "FALL"
                                           : "WINTER";
    const int nextYear = month <= 9 ? year : year + 1;

    const std::string thisSeasonArgs = ", sort: POPULARITY_DESC, season: " +
                                       std::string(season) + ", seasonYear: " +
                                       std::to_string(year);
    const std::string nextSeasonArgs = ", sort: POPULARITY_DESC, season: " +
                                       std::string(nextSeason) + ", seasonYear: " +
                                       std::to_string(nextYear) +
                                       ", status: NOT_YET_RELEASED";

    return {
        {"trending", "Trending now", ", sort: TRENDING_DESC"},
        {"season", "This season", thisSeasonArgs},
        {"airing", "Airing now", ", sort: TRENDING_DESC, status: RELEASING"},
        {"upcoming", "Coming next season", nextSeasonArgs},
        {"rated", "Highest rated", ", sort: SCORE_DESC"},
        {"popular", "Most popular of all time", ", sort: POPULARITY_DESC"},
        {"movies", "Films", ", sort: SCORE_DESC, format: MOVIE"},
        {"fresh", "Recently started", ", sort: START_DATE_DESC, status: RELEASING"},
        {"classics", "Classics", ", sort: SCORE_DESC, startDate_lesser: 20100101"},
    };
}

const char* kShelfFields =
    "      id episodes format status seasonYear averageScore genres"
    "      title { romaji english }"
    "      coverImage { large color }";

}  // namespace

std::vector<DiscoverShelf> discoverShelves(const std::string& genre, bool allowAdult,
                                           std::string* error) {
    std::vector<DiscoverShelf> out;
    if (error) error->clear();

    const std::string genreArg = genre.empty() ? "" : ", genre: $genre";
    const std::string adultArg = allowAdult ? "" : ", isAdult: false";
    const std::string decls = genre.empty() ? "" : "($genre: String)";

    const auto specs = shelfSpecs();

    std::string gql = "query " + decls + " {";
    for (const auto& spec : specs) {
        gql += std::string("  ") + spec.key + ": Page(page: 1, perPage: 24) {" +
               "    media(type: ANIME" + spec.args + genreArg + adultArg + ") {" +
               kShelfFields + "    }" + "  }";
    }
    gql += "}";

    json body;
    body["query"] = gql;
    if (!genre.empty()) body["variables"] = json{{"genre", genre}};

    const auto res = post(body.dump(), "");
    if (!res.ok) {
        if (error) {
            *error = res.status == 429
                         ? "AniList is rate limiting us - give it a minute."
                         : "Could not reach AniList.";
        }
        return out;
    }

    json j;
    try {
        j = json::parse(res.body);
    } catch (const std::exception&) {
        if (error) *error = "AniList sent something unreadable.";
        return out;
    }

    // A 429 comes back as a normal body with an errors array, not as a
    // transport failure, so it has to be looked for here too.
    if (j.contains("errors") && j["errors"].is_array() && !j["errors"].empty()) {
        const std::string msg = j["errors"][0].value("message", "AniList refused the request.");
        if (error) {
            *error = msg == "Too Many Requests."
                         ? "AniList is rate limiting us - give it a minute."
                         : msg;
        }
        return out;
    }

    if (!j.contains("data") || !j["data"].is_object()) return out;
    const json& data = j["data"];

    for (const auto& spec : specs) {
        if (!data.contains(spec.key) || !data[spec.key].is_object()) continue;
        DiscoverShelf s;
        s.key = spec.key;
        s.title = spec.title;
        for (const auto& m : data[spec.key].value("media", json::array())) {
            if (!m.is_object()) continue;
            BrowseItem b = parseBrowseItem(m);
            if (b.id) s.items.push_back(std::move(b));
        }
        if (!s.items.empty()) out.push_back(std::move(s));
    }
    return out;
}

std::vector<BrowseItem> shelfPage(const std::string& key, const std::string& genre,
                                  bool allowAdult, int page, std::string* error) {
    std::vector<BrowseItem> out;
    if (error) error->clear();

    std::string args;
    for (const auto& spec : shelfSpecs()) {
        if (key == spec.key) args = spec.args;
    }
    if (args.empty()) return out;

    const std::string genreArg = genre.empty() ? "" : ", genre: $genre";
    const std::string adultArg = allowAdult ? "" : ", isAdult: false";

    std::string decls = "($page: Int";
    if (!genre.empty()) decls += ", $genre: String";
    decls += ")";

    const std::string gql = "query " + decls + " {" +
                            "  Page(page: $page, perPage: 30) {" +
                            "    media(type: ANIME" + args + genreArg + adultArg + ") {" +
                            kShelfFields + "    }" + "  }" + "}";

    json vars{{"page", page < 1 ? 1 : page}};
    if (!genre.empty()) vars["genre"] = genre;

    json body;
    body["query"] = gql;
    body["variables"] = vars;

    const auto res = post(body.dump(), "");
    if (!res.ok) {
        if (error) {
            *error = res.status == 429 ? "AniList is rate limiting us - give it a minute."
                                       : "Could not reach AniList.";
        }
        return out;
    }

    try {
        const json j = json::parse(res.body);
        if (j.contains("errors") && j["errors"].is_array() && !j["errors"].empty()) {
            if (error) {
                *error = j["errors"][0].value("message", "AniList refused the request.");
            }
            return out;
        }
        for (const auto& m : j["data"]["Page"].value("media", json::array())) {
            if (!m.is_object()) continue;
            BrowseItem b = parseBrowseItem(m);
            if (b.id) out.push_back(std::move(b));
        }
    } catch (const std::exception&) {
        if (error) *error = "AniList sent something unreadable.";
    }
    return out;
}

std::vector<BrowseItem> browse(const BrowseFilters& f) {
    std::vector<BrowseItem> out;

    // Unset filters are omitted from the query rather than passed as null.
    // AniList reads a null `status` as "status IS NULL" and returns nothing,
    // which silently emptied every browse that did not filter by status.
    std::string decls = "$page: Int, $sort: [MediaSort]";
    std::string args = "type: ANIME, sort: $sort";

    json vars;
    vars["page"] = f.page < 1 ? 1 : f.page;
    vars["sort"] = json::array({f.sort.empty() ? "POPULARITY_DESC" : f.sort});

    const auto add = [&](const char* decl, const char* arg, const char* var,
                         const json& value) {
        decls += ", ";
        decls += decl;
        args += ", ";
        args += arg;
        vars[var] = value;
    };

    if (!f.search.empty()) add("$search: String", "search: $search", "search", f.search);
    if (!f.genre.empty()) add("$genre: String", "genre: $genre", "genre", f.genre);
    if (!f.season.empty()) add("$season: MediaSeason", "season: $season", "season", f.season);
    if (!f.format.empty()) add("$format: MediaFormat", "format: $format", "format", f.format);
    if (!f.status.empty()) add("$status: MediaStatus", "status: $status", "status", f.status);
    if (f.year > 0) add("$year: Int", "seasonYear: $year", "year", f.year);
    if (!f.allowAdult) add("$adult: Boolean", "isAdult: $adult", "adult", false);

    const std::string gql =
        "query (" + decls + ") {"
        "  Page(page: $page, perPage: 30) {"
        "    media(" + args + ") {"
        "      id episodes format status seasonYear averageScore genres"
        "      title { romaji english }"
        "      coverImage { large color }"
        "    }"
        "  }"
        "}";

    json body;
    body["query"] = gql;
    body["variables"] = vars;

    const auto res = post(body.dump(), "");
    if (!res.ok) return out;

    json j;
    try {
        j = json::parse(res.body);
    } catch (const std::exception&) {
        return out;
    }
    if (!j.is_object() || !j.contains("data") || !j["data"].is_object()) return out;
    const json& data = j["data"];
    if (!data.contains("Page") || !data["Page"].is_object()) return out;

    for (const auto& m : data["Page"].value("media", json::array())) {
        if (!m.is_object()) continue;
        BrowseItem b;
        b.id = intOr(m.contains("id") ? m["id"] : json(), 0);
        if (!b.id) continue;
        b.episodes = intOr(m.contains("episodes") ? m["episodes"] : json(), 0);
        b.year = intOr(m.contains("seasonYear") ? m["seasonYear"] : json(), 0);
        b.score = intOr(m.contains("averageScore") ? m["averageScore"] : json(), 0);
        b.format = pick(m, "format");
        b.status = pick(m, "status");

        const json t = m.value("title", json::object());
        b.title = pick(t, "romaji");
        if (b.title.empty()) b.title = pick(t, "english");

        const json ci = m.value("coverImage", json::object());
        b.cover = pick(ci, "large");
        b.color = pick(ci, "color");

        for (const auto& g : m.value("genres", json::array())) {
            if (g.is_string()) b.genres.push_back(g.get<std::string>());
        }
        out.push_back(std::move(b));
    }
    return out;
}

std::vector<AiringItem> airing(int days) {
    const long long now = static_cast<long long>(std::time(nullptr));
    return airingBetween(now, now + static_cast<long long>(days) * 86400);
}

// The same shape, filtered to specific shows. AniList rejects a null id list
// rather than ignoring it, so the filtered and unfiltered forms have to be two
// query strings even though everything below the first line is identical.
constexpr const char* kMyAiringQuery = R"(
query ($ids: [Int], $from: Int, $to: Int, $page: Int) {
  Page(page: $page, perPage: 50) {
    pageInfo { hasNextPage }
    airingSchedules(mediaId_in: $ids, airingAt_greater: $from, airingAt_lesser: $to, sort: TIME) {
      episode
      airingAt
      media {
        id
        title { romaji english }
        coverImage { large color }
        isAdult
      }
    }
  }
}
)";

// One page of an airing window, optionally narrowed to a set of shows.
std::vector<AiringItem> airingPage(long long from, long long to, int page,
                                   const std::vector<int>* onlyThese, bool* hasNext) {
    std::vector<AiringItem> out;
    if (hasNext) *hasNext = false;
    if (onlyThese && onlyThese->empty()) return out;

    json vars;
    vars["from"] = from;
    vars["to"] = to;
    vars["page"] = page;
    if (onlyThese) vars["ids"] = *onlyThese;

    json body;
    body["query"] = onlyThese ? kMyAiringQuery : kAiringQuery;
    body["variables"] = vars;

    const auto res = post(body.dump(), "");
    if (!res.ok) return out;

    json j;
    try {
        j = json::parse(res.body);
    } catch (const std::exception&) {
        return out;
    }
    if (!j.is_object() || !j.contains("data") || !j["data"].is_object()) return out;
    const json& data = j["data"];
    if (!data.contains("Page") || !data["Page"].is_object()) return out;

    for (const auto& a : data["Page"].value("airingSchedules", json::array())) {
        if (!a.is_object()) continue;
        const json m = a.value("media", json::object());
        if (!m.is_object()) continue;
        if (m.contains("isAdult") && m["isAdult"].is_boolean() && m["isAdult"].get<bool>()) {
            continue;
        }

        AiringItem it;
        it.mediaId = intOr(m.contains("id") ? m["id"] : json(), 0);
        if (!it.mediaId) continue;
        it.episode = intOr(a.contains("episode") ? a["episode"] : json(), 0);
        it.airingAt = a.contains("airingAt") && a["airingAt"].is_number()
                          ? a["airingAt"].get<long long>()
                          : 0;

        const json t = m.value("title", json::object());
        it.title = pick(t, "romaji");
        if (it.title.empty()) it.title = pick(t, "english");

        const json ci = m.value("coverImage", json::object());
        it.cover = pick(ci, "large");
        it.color = pick(ci, "color");

        out.push_back(std::move(it));
    }

    const json info = data["Page"].value("pageInfo", json::object());
    if (hasNext) *hasNext = info.value("hasNextPage", false);
    return out;
}

// The whole window at once, for callers that cannot show partial results.
//
// Capped, because an unbounded loop against a thirty-a-minute budget is a way
// to lock the whole app out over one calendar view.
std::vector<AiringItem> airingBetween(long long from, long long to) {
    std::vector<AiringItem> out;
    for (int page = 1; page <= kAiringPageCap; ++page) {
        bool hasNext = false;
        auto part = airingPage(from, to, page, nullptr, &hasNext);
        out.insert(out.end(), std::make_move_iterator(part.begin()),
                   std::make_move_iterator(part.end()));
        if (!hasNext) break;
    }
    return out;
}

std::vector<std::string> genres() {
    // AniList's genre set is fixed and small; asking for it every time would
    // be a round trip for a list that has not changed in years.
    return {"Action", "Adventure", "Comedy", "Drama", "Ecchi", "Fantasy",
            "Horror", "Mahou Shoujo", "Mecha", "Music", "Mystery", "Psychological",
            "Romance", "Sci-Fi", "Slice of Life", "Sports", "Supernatural",
            "Thriller"};
}

}  // namespace tsuzuki::anilist
