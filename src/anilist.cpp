#include "anilist.hpp"

#include <nlohmann/json.hpp>

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

}  // namespace tsuzuki::anilist
