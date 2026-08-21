#include "anilist.hpp"

#include <nlohmann/json.hpp>

#include <cctype>

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

}  // namespace tsuzuki::anilist
