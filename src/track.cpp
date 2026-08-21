#include "track.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "http.hpp"

namespace tsuzuki::track {
namespace {

using nlohmann::json;

constexpr const char* kEndpoint = "https://graphql.anilist.co";

// Registered once for Tsuzuki itself. Public identifier, no secret.
constexpr const char* kDefaultClientId = "";

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

std::string defaultClientId() { return kDefaultClientId; }

std::string resolveClientId(const std::string& fromSettings) {
    return fromSettings.empty() ? std::string(kDefaultClientId) : fromSettings;
}

std::string authorizeUrl(const std::string& clientId, int port) {
    if (clientId.empty()) return {};
    // Implicit grant: AniList hands the token back in the URL fragment, so it
    // never touches a server and no client secret is needed.
    const std::string redirect =
        "http://127.0.0.1:" + std::to_string(port) + "/auth/anilist";
    return "https://anilist.co/api/v2/oauth/authorize?client_id=" +
           http::urlEncode(clientId) + "&response_type=token&redirect_uri=" +
           http::urlEncode(redirect);
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
