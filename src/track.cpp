#include "track.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include "http.hpp"

// Optional, gitignored. Defines TSUZUKI_ANILIST_CLIENT_ID and
// TSUZUKI_ANILIST_CLIENT_SECRET so a built binary can link in one click
// without those credentials living in a public repository.
#if __has_include("secrets.local.hpp")
#include "secrets.local.hpp"
#endif

#ifndef TSUZUKI_ANILIST_CLIENT_ID
#define TSUZUKI_ANILIST_CLIENT_ID ""
#endif
#ifndef TSUZUKI_ANILIST_CLIENT_SECRET
#define TSUZUKI_ANILIST_CLIENT_SECRET ""
#endif

namespace tsuzuki::track {
namespace {

using nlohmann::json;

constexpr const char* kEndpoint = "https://graphql.anilist.co";

constexpr const char* kTokenEndpoint = "https://anilist.co/api/v2/oauth/token";

std::mutex g_mutex;
std::string g_token;
Account g_account;
bool g_accountFetched = false;

std::string authPath() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki" : std::string(".");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\auth.json";
}

// Writes and reads back. A credential that silently fails to save looks
// exactly like being signed out on the next launch, which is what happened.
bool persist() {
    const std::string path = authPath();
    const std::string payload = json{{"anilist", g_token}}.dump(2) + "\n";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << payload;
        out.flush();
        if (!out) return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::string back((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    return back == payload;
}

// nlohmann value() only substitutes the default when a key is ABSENT. AniList
// sends keys that are present and null - list group status, for one - and
// converting null to std::string throws. Everything below tolerates null.
std::string safeStr(const json& obj, const char* key) {
    if (!obj.is_object() || !obj.contains(key)) return {};
    const json& v = obj[key];
    return v.is_string() ? v.get<std::string>() : std::string();
}

int safeInt(const json& obj, const char* key, int fallback = 0) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const json& v = obj[key];
    return v.is_number_integer() ? v.get<int>() : fallback;
}

std::string pick(const json& obj, const char* key) {
    if (!obj.is_object() || !obj.contains(key) || obj[key].is_null()) return {};
    return obj[key].is_string() ? obj[key].get<std::string>() : std::string();
}

struct QueryResult {
    json data;
    long status = 0;
    bool reachedServer = false;
};

QueryResult queryEx(const std::string& gql, const json& variables, const std::string& tok) {
    json body;
    body["query"] = gql;
    if (!variables.is_null()) body["variables"] = variables;

    const auto res = http::postJson(kEndpoint, body.dump(), 20, tok);

    QueryResult out;
    out.status = res.status;
    out.reachedServer = res.status > 0;
    try {
        out.data = json::parse(res.body);
    } catch (const std::exception&) {
    }
    return out;
}

json query(const std::string& gql, const json& variables, const std::string& tok) {
    return queryEx(gql, variables, tok).data;
}

}  // namespace

std::string defaultClientId() { return TSUZUKI_ANILIST_CLIENT_ID; }
std::string defaultClientSecret() { return TSUZUKI_ANILIST_CLIENT_SECRET; }

std::string resolveClientId(const std::string& fromSettings) {
    return fromSettings.empty() ? defaultClientId() : fromSettings;
}

std::string resolveClientSecret(const std::string& fromSettings) {
    return fromSettings.empty() ? defaultClientSecret() : fromSettings;
}

// Whatever redirect the AniList client is actually registered with. It does
// not have to be our loopback - it only has to match, and the code can be
// handed back by hand when it is somewhere we cannot listen on.
std::string authorizeUrl(const std::string& clientId, const std::string& redirect) {
    if (clientId.empty()) return {};
    return "https://anilist.co/api/v2/oauth/authorize?client_id=" +
           http::urlEncode(clientId) + "&response_type=code&redirect_uri=" +
           http::urlEncode(redirect);
}

bool exchangeCode(const std::string& code, const std::string& clientId,
                  const std::string& clientSecret, const std::string& redirect,
                  std::string& error) {
    if (code.empty()) {
        error = "AniList did not return an authorization code.";
        return false;
    }
    if (clientSecret.empty()) {
        error = "No client secret available - AniList needs one for this flow.";
        return false;
    }

    const json body{{"grant_type", "authorization_code"},
                    {"client_id", clientId},
                    {"client_secret", clientSecret},
                    {"redirect_uri", redirect},
                    {"code", code}};

    const auto res = http::postJson(kTokenEndpoint, body.dump());
    json j;
    try {
        j = json::parse(res.body);
    } catch (const std::exception&) {
        error = "AniList returned something unreadable (HTTP " +
                std::to_string(res.status) + ").";
        return false;
    }

    if (j.contains("access_token") && j["access_token"].is_string()) {
        setToken(j["access_token"].get<std::string>());
        return true;
    }

    // Surface AniList's own wording; it is usually the actual problem, most
    // often a redirect URL that does not match the registered one.
    error = j.value("hint", j.value("message", j.value("error", "AniList rejected the login.")));
    return false;
}

std::string implicitAuthorizeUrl(const std::string& clientId) {
    if (clientId.empty()) return {};
    return "https://anilist.co/api/v2/oauth/authorize?client_id=" +
           http::urlEncode(clientId) + "&response_type=token";
}

void setToken(const std::string& tok) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_token = tok;
    g_accountFetched = false;
    g_account = Account{};
    persist();
}

std::string token() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_token;
}

bool linked() { return !token().empty(); }

void logout() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_token.clear();
    g_account = Account{};
    g_accountFetched = false;
    persist();
}

void load() {
    std::ifstream in(authPath());
    if (!in) return;
    try {
        json j;
        in >> j;
        std::lock_guard<std::mutex> lock(g_mutex);
        if (j.contains("anilist") && j["anilist"].is_string()) {
            g_token = j["anilist"].get<std::string>();
        }
    } catch (const std::exception&) {
    }
}

Account account(bool force) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_accountFetched && !force) return g_account;
        if (g_token.empty()) return Account{};
    }

    const std::string tok = token();
    const QueryResult r = queryEx("query { Viewer { id name avatar { medium } } }", json(), tok);
    const json& j = r.data;

    Account a;
    if (j.contains("data") && !j["data"].is_null() && !j["data"]["Viewer"].is_null()) {
        const auto& v = j["data"]["Viewer"];
        a.linked = true;
        a.id = v.value("id", 0);
        a.name = v.value("name", "");
        if (v.contains("avatar") && v["avatar"].is_object()) {
            a.avatar = v["avatar"].value("medium", "");
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        g_account = a;
        g_accountFetched = true;
        // Self-heal: if an earlier write failed, a token we have just proved
        // works is worth another attempt at saving.
        persist();
        return a;
    }

    // Only forget the credential when AniList has actually rejected it.
    // Anything else - no network, a timeout, a 500, an unparseable body - is
    // temporary, and throwing the token away over it silently signs the user
    // out. That is what kept happening.
    const bool rejected = r.reachedServer && (r.status == 400 || r.status == 401);
    if (rejected) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_token.clear();
        g_account = Account{};
        g_accountFetched = true;
        persist();
        return Account{};
    }

    // Keep the token, report the account as still linked from cache if we have
    // it, and try again next time.
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_account.linked ? g_account : Account{};
}

bool updateProgress(int mediaId, int progress) {
    if (mediaId <= 0 || progress <= 0) return false;
    const std::string tok = token();
    if (tok.empty()) return false;

    const json vars{{"id", mediaId}, {"p", progress}};
    const json j = query(
        "mutation ($id: Int, $p: Int) {"
        "  SaveMediaListEntry(mediaId: $id, progress: $p, status: CURRENT) {"
        "    id progress status"
        "  }"
        "}",
        vars, tok);

    return j.contains("data") && !j["data"].is_null() &&
           !j["data"]["SaveMediaListEntry"].is_null();
}

std::vector<ListEntry> lists() {
    std::vector<ListEntry> out;
    const Account a = account();
    if (!a.linked) return out;

    // Shape taken from hayase-app/interface queries.ts: no status filter, sort
    // by recent activity, and let the caller decide what to show. Passing
    // status_in here is what made AniList return an error instead of data.
    const json vars{{"id", a.id}};
    const json j = query(
        "query ($id: Int) {"
        "  MediaListCollection(userId: $id, type: ANIME,"
        "                      forceSingleCompletedList: true,"
        "                      sort: UPDATED_TIME_DESC) {"
        "    lists { status entries { progress media {"
        "      id episodes status title { romaji english }"
        "      coverImage { large color } } } }"
        "  }"
        "}",
        vars, token());

    // Defensive throughout: AniList answers errors with a completely different
    // shape, and indexing a const json at a missing key throws.
    if (!j.is_object() || !j.contains("data") || !j["data"].is_object()) return out;
    const json& data = j["data"];
    if (!data.contains("MediaListCollection") || !data["MediaListCollection"].is_object()) {
        return out;
    }

    for (const auto& list : data["MediaListCollection"].value("lists", json::array())) {
        if (!list.is_object()) continue;
        const std::string status = safeStr(list, "status");
        for (const auto& e : list.value("entries", json::array())) {
            if (!e.is_object()) continue;
            const json m = e.value("media", json::object());
            if (!m.is_object()) continue;

            ListEntry le;
            le.mediaId = safeInt(m, "id");
            le.progress = safeInt(e, "progress");
            le.episodes = safeInt(m, "episodes");
            le.status = status;
            le.airing = safeStr(m, "status");

            const json t = m.value("title", json::object());
            le.title = pick(t, "romaji");
            if (le.title.empty()) le.title = pick(t, "english");

            const json ci = m.value("coverImage", json::object());
            le.cover = pick(ci, "large");
            le.color = pick(ci, "color");

            // Next unwatched episode, never past the end of a finished show.
            le.nextEpisode = le.progress + 1;
            if (le.episodes > 0 && le.nextEpisode > le.episodes) le.nextEpisode = le.episodes;

            if (!le.mediaId) continue;

            // The same show comes back once per list it belongs to, including
            // unnamed custom lists with a null status. Keep one, preferring
            // the copy that actually says what it is.
            auto existing = std::find_if(out.begin(), out.end(),
                                         [&](const ListEntry& x) {
                                             return x.mediaId == le.mediaId;
                                         });
            if (existing != out.end()) {
                if (existing->status.empty() && !le.status.empty()) existing->status = le.status;
                continue;
            }
            out.push_back(std::move(le));
        }
    }
    return out;
}

}  // namespace tsuzuki::track
