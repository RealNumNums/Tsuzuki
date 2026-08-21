#include "track.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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

void persist() {
    std::ofstream out(authPath());
    if (out) out << json{{"anilist", g_token}}.dump(2) << "\n";
}

json query(const std::string& gql, const json& variables, const std::string& tok) {
    json body;
    body["query"] = gql;
    if (!variables.is_null()) body["variables"] = variables;

    const auto res = http::postJson(kEndpoint, body.dump(), 20, tok);
    if (!res.ok) return json();
    try {
        return json::parse(res.body);
    } catch (const std::exception&) {
        return json();
    }
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
    const json j = query("query { Viewer { id name avatar { medium } } }", json(), tok);

    Account a;
    if (j.contains("data") && !j["data"].is_null() && !j["data"]["Viewer"].is_null()) {
        const auto& v = j["data"]["Viewer"];
        a.linked = true;
        a.id = v.value("id", 0);
        a.name = v.value("name", "");
        if (v.contains("avatar") && v["avatar"].is_object()) {
            a.avatar = v["avatar"].value("medium", "");
        }
    } else {
        // A token that no longer works is worse than none: it would fail
        // silently on every sync. Drop it so the UI shows "not linked".
        std::lock_guard<std::mutex> lock(g_mutex);
        g_token.clear();
        persist();
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    g_account = a;
    g_accountFetched = true;
    return a;
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

}  // namespace tsuzuki::track
