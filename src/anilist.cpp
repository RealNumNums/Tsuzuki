#include "anilist.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <ctime>

#include "http.hpp"

namespace tsuzuki::anilist {

using nlohmann::json;

namespace {

constexpr const char* kEndpoint = "https://graphql.anilist.co";

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

std::vector<Media> search(const std::string& text) {
    std::vector<Media> out;

    json body;
    body["query"] = kSearchQuery;
    body["variables"]["search"] = text;

    const auto res = http::postJson(kEndpoint, body.dump());
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
    title { romaji english }
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

    const auto res = http::postJson(kEndpoint, body.dump());
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
    out.episodes = m.contains("episodes") && !m["episodes"].is_null()
                       ? m["episodes"].get<int>() : 0;
    out.duration = m.contains("duration") && !m["duration"].is_null()
                       ? m["duration"].get<int>() : 0;

    const auto& t = m["title"];
    out.title = pick(t, "romaji");
    if (out.title.empty()) out.title = pick(t, "english");

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
query ($from: Int, $to: Int) {
  Page(page: 1, perPage: 50) {
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

    const auto res = http::postJson(kEndpoint, body.dump());
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
    std::vector<AiringItem> out;

    const long long now = static_cast<long long>(std::time(nullptr));
    json vars;
    vars["from"] = now;
    vars["to"] = now + static_cast<long long>(days) * 86400;

    json body;
    body["query"] = kAiringQuery;
    body["variables"] = vars;

    const auto res = http::postJson(kEndpoint, body.dump());
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
                          ? a["airingAt"].get<long long>() : 0;

        const json t = m.value("title", json::object());
        it.title = pick(t, "romaji");
        if (it.title.empty()) it.title = pick(t, "english");

        const json ci = m.value("coverImage", json::object());
        it.cover = pick(ci, "large");
        it.color = pick(ci, "color");

        out.push_back(std::move(it));
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
