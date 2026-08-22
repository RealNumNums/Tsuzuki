// MyAnimeList, Simkl and Kitsu.
//
// Three services, three ways in, one job: when an episode is finished, say so.
//
// Reading their lists is deliberately not implemented. The primary service
// supplies the library, the artwork and the cross-service id; merging four
// opinions of the same list is a different problem, and pretending to do it
// badly would be worse than not doing it. Each returns an empty list and
// reports success, which the sync layer already treats as "nothing to merge"
// rather than as a failure.

#include "tracker.hpp"

#include "http.hpp"
#include "settings.hpp"
#include "ui.hpp"

// Optional, gitignored, same as the AniList credentials.
#if __has_include("secrets.local.hpp")
#include "secrets.local.hpp"
#endif
#ifndef TSUZUKI_MAL_CLIENT_ID
#define TSUZUKI_MAL_CLIENT_ID ""
#endif
#ifndef TSUZUKI_SIMKL_CLIENT_ID
#define TSUZUKI_SIMKL_CLIENT_ID ""
#endif

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace tsuzuki::tracker {
namespace {

using nlohmann::json;

// ---------------------------------------------------------------- storage

// One file for every service's credentials, beside the AniList one. Kept
// separate from auth.json so a botched write to a new tracker cannot cost
// somebody their working AniList link.
std::mutex g_storeMutex;
json g_store = json::object();
bool g_storeLoaded = false;

std::string storePath() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki" : std::string(".");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\trackers.json";
}

void loadStore() {
    std::lock_guard<std::mutex> lock(g_storeMutex);
    if (g_storeLoaded) return;
    g_storeLoaded = true;
    std::ifstream in(storePath());
    if (!in) return;
    try {
        json j;
        in >> j;
        if (j.is_object()) g_store = std::move(j);
    } catch (const std::exception&) {
    }
}

void saveStore() {
    std::ofstream out(storePath(), std::ios::binary | std::ios::trunc);
    if (out) out << g_store.dump(2) << "\n";
}

json creds(const std::string& service) {
    loadStore();
    std::lock_guard<std::mutex> lock(g_storeMutex);
    return g_store.contains(service) && g_store[service].is_object() ? g_store[service]
                                                                     : json::object();
}

void setCreds(const std::string& service, const json& value) {
    loadStore();
    std::lock_guard<std::mutex> lock(g_storeMutex);
    g_store[service] = value;
    saveStore();
}

void clearCreds(const std::string& service) {
    loadStore();
    std::lock_guard<std::mutex> lock(g_storeMutex);
    g_store.erase(service);
    saveStore();
}

std::string str(const json& j, const char* key) {
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : std::string();
}

// ============================================================ MyAnimeList

// OAuth with PKCE. MAL is unusual in only supporting the "plain" challenge
// method, so the verifier and the challenge are the same string - which is
// fine here because it never leaves this machine except over TLS.
class MyAnimeList final : public Service {
  public:
    const char* id() const override { return "mal"; }
    const char* name() const override { return "MyAnimeList"; }
    AuthKind authKind() const override { return AuthKind::HostedLogin; }

    bool configured() const override { return !clientId().empty(); }
    std::string configHint() const override {
        return "No Client ID is built in. Add one from myanimelist.net/apiconfig.";
    }

    void load() override { loadStore(); }
    bool linked() const override { return !str(creds("mal"), "access_token").empty(); }

    Account account(bool force) override {
        Account out;
        const std::string token = str(creds("mal"), "access_token");
        if (token.empty()) return out;
        if (!force && !cachedName_.empty()) {
            out.linked = true;
            out.name = cachedName_;
            return out;
        }
        const auto res = http::get("https://api.myanimelist.net/v2/users/@me", 15, bearer(token));
        if (!res.ok) {
            // Only forget a credential the server has actually rejected.
            if (res.status == 401) logout();
            return out;
        }
        try {
            const json j = json::parse(res.body);
            out.linked = true;
            out.name = j.value("name", "");
            out.id = std::to_string(j.value("id", 0));
            cachedName_ = out.name;
        } catch (const std::exception&) {
        }
        return out;
    }

    void logout() override {
        cachedName_.clear();
        clearCreds("mal");
    }

    std::string authorizeUrl() const override {
        if (!configured()) return {};
        // The verifier is generated per attempt and kept until the code comes
        // back. Plain method, because MAL rejects S256.
        verifier_ = makeVerifier();
        return "https://myanimelist.net/v1/oauth2/authorize?response_type=code&client_id=" +
               clientId() + "&code_challenge=" + verifier_ + "&code_challenge_method=plain";
    }

    bool acceptRedirect(const std::string& url) override {
        const auto at = url.find("code=");
        if (at == std::string::npos) return false;
        std::string code = url.substr(at + 5);
        const auto end = code.find_first_of("&#");
        if (end != std::string::npos) code = code.substr(0, end);
        if (code.empty() || verifier_.empty()) return false;

        const std::string body = "client_id=" + clientId() + "&code=" + code +
                                 "&code_verifier=" + verifier_ +
                                 "&grant_type=authorization_code";
        const auto res = http::postForm("https://myanimelist.net/v1/oauth2/token", body, 20);
        if (!res.ok) return false;
        try {
            const json j = json::parse(res.body);
            const std::string token = j.value("access_token", "");
            if (token.empty()) return false;
            setCreds("mal", json{{"access_token", token},
                                 {"refresh_token", j.value("refresh_token", "")}});
            verifier_.clear();
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    std::vector<Entry> lists(bool* ok) override {
        if (ok) *ok = true;  // written to, not read from
        return {};
    }

    bool update(const MediaRef& what, int progress, const std::string& status) override {
        if (what.malId <= 0) return false;
        const std::string token = str(creds("mal"), "access_token");
        if (token.empty()) return false;

        std::string body = "status=" + malStatus(status);
        if (progress > 0) body += "&num_watched_episodes=" + std::to_string(progress);

        const auto res = http::patchForm(
            "https://api.myanimelist.net/v2/anime/" + std::to_string(what.malId) +
                "/my_list_status",
            body, 20, bearer(token));
        return res.ok;
    }

  private:
    static std::string malStatus(const std::string& s) {
        if (s == "COMPLETED") return "completed";
        if (s == "PAUSED") return "on_hold";
        if (s == "DROPPED") return "dropped";
        if (s == "PLANNING") return "plan_to_watch";
        return "watching";
    }
    static std::string bearer(const std::string& t) { return t; }
    // The built-in id unless somebody has pointed this at their own app.
    static std::string clientId() {
        const std::string fromSettings = ui::settings().malClientId;
        return fromSettings.empty() ? std::string(TSUZUKI_MAL_CLIENT_ID) : fromSettings;
    }
    static std::string makeVerifier() {
        // 64 URL-safe characters, which is inside MAL's 43-128 range.
        static const char* alphabet =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~";
        std::string out;
        for (int i = 0; i < 64; ++i) out += alphabet[std::rand() % 64];
        return out;
    }

    mutable std::string verifier_;
    std::string cachedName_;
};

// =================================================================== Simkl

// Device code: the app shows a PIN, the user types it in on simkl.com, and the
// app asks every few seconds whether that has happened. No redirect URL and no
// password, which makes it the pleasantest of the three on a desktop.
class Simkl final : public Service {
  public:
    const char* id() const override { return "simkl"; }
    const char* name() const override { return "Simkl"; }
    AuthKind authKind() const override { return AuthKind::DeviceCode; }

    bool configured() const override { return !clientId().empty(); }
    std::string configHint() const override {
        return "No Client ID is built in. Add one from simkl.com developer settings.";
    }

    void load() override { loadStore(); }
    bool linked() const override { return !str(creds("simkl"), "access_token").empty(); }

    Account account(bool force) override {
        Account out;
        const std::string token = str(creds("simkl"), "access_token");
        if (token.empty()) return out;
        if (!force && !cachedName_.empty()) {
            out.linked = true;
            out.name = cachedName_;
            return out;
        }
        const auto res = http::get("https://api.simkl.com/users/settings?client_id=" + clientId(),
                                   15, token);
        if (!res.ok) {
            if (res.status == 401) logout();
            return out;
        }
        try {
            const json j = json::parse(res.body);
            out.linked = true;
            if (j.contains("user") && j["user"].is_object()) {
                out.name = j["user"].value("name", "");
            }
            cachedName_ = out.name;
        } catch (const std::exception&) {
        }
        return out;
    }

    void logout() override {
        cachedName_.clear();
        clearCreds("simkl");
    }

    DeviceCode beginDeviceCode() override {
        DeviceCode out;
        if (!configured()) {
            out.error = configHint();
            return out;
        }
        const auto res =
            http::get("https://api.simkl.com/oauth/pin?client_id=" + clientId(), 20);
        if (!res.ok) {
            out.error = "Could not reach Simkl.";
            return out;
        }
        try {
            const json j = json::parse(res.body);
            out.userCode = j.value("user_code", "");
            out.verificationUrl = j.value("verification_url", "https://simkl.com/pin");
            out.intervalSeconds = j.value("interval", 5);
            out.ok = !out.userCode.empty();
            pending_ = out.userCode;
            if (!out.ok) out.error = "Simkl did not issue a code.";
        } catch (const std::exception&) {
            out.error = "Simkl sent something unreadable.";
        }
        return out;
    }

    bool pollDeviceCode(std::string& error) override {
        if (pending_.empty()) return false;
        const auto res = http::get(
            "https://api.simkl.com/oauth/pin/" + pending_ + "?client_id=" + clientId(), 15);
        if (!res.ok) return false;
        try {
            const json j = json::parse(res.body);
            const std::string result = j.value("result", "");
            if (result == "OK") {
                const std::string token = j.value("access_token", "");
                if (token.empty()) return false;
                setCreds("simkl", json{{"access_token", token}});
                pending_.clear();
                return true;
            }
            if (result == "KO") return false;  // still waiting; not an error
            error = j.value("message", "");
        } catch (const std::exception&) {
        }
        return false;
    }

    std::vector<Entry> lists(bool* ok) override {
        if (ok) *ok = true;
        return {};
    }

    bool update(const MediaRef& what, int progress, const std::string& status) override {
        if (what.malId <= 0 || progress <= 0) return false;
        const std::string token = str(creds("simkl"), "access_token");
        if (token.empty()) return false;

        // Simkl records episodes as watched history rather than a count, so
        // every episode up to this one is sent. It de-duplicates on its side.
        json episodes = json::array();
        for (int i = 1; i <= progress; ++i) episodes.push_back(json{{"number", i}});

        const json body{
            {"shows", json::array({json{{"ids", json{{"mal", what.malId}}},
                                        {"seasons", json::array({json{{"number", 1},
                                                                      {"episodes", episodes}}})}}})}};

        const auto res = http::postJson("https://api.simkl.com/sync/history?client_id=" +
                                            clientId(),
                                        body.dump(), 20, token);
        return res.ok;
    }

  private:
    static std::string clientId() {
        const std::string fromSettings = ui::settings().simklClientId;
        return fromSettings.empty() ? std::string(TSUZUKI_SIMKL_CLIENT_ID) : fromSettings;
    }

    std::string pending_;
    std::string cachedName_;
};

// =================================================================== Kitsu

// Kitsu has no consent page - only a password grant - so linking means asking
// for the password directly. Only the token it returns is kept.
class Kitsu final : public Service {
  public:
    const char* id() const override { return "kitsu"; }
    const char* name() const override { return "Kitsu"; }
    AuthKind authKind() const override { return AuthKind::Password; }

    std::string configHint() const override {
        return "Kitsu has no consent page, so this asks for your password directly. "
               "Only the token it returns is stored.";
    }

    void load() override { loadStore(); }
    bool linked() const override { return !str(creds("kitsu"), "access_token").empty(); }

    Account account(bool force) override {
        Account out;
        const std::string token = str(creds("kitsu"), "access_token");
        if (token.empty()) return out;
        if (!force && !cachedName_.empty()) {
            out.linked = true;
            out.name = cachedName_;
            out.id = cachedId_;
            return out;
        }
        const auto res = http::get("https://kitsu.app/api/edge/users?filter[self]=true", 15, token);
        if (!res.ok) {
            if (res.status == 401) logout();
            return out;
        }
        try {
            const json j = json::parse(res.body);
            const auto& data = j["data"];
            if (data.is_array() && !data.empty()) {
                out.linked = true;
                cachedId_ = data[0].value("id", "");
                out.id = cachedId_;
                if (data[0].contains("attributes")) {
                    out.name = data[0]["attributes"].value("name", "");
                    cachedName_ = out.name;
                }
            }
        } catch (const std::exception&) {
        }
        return out;
    }

    void logout() override {
        cachedName_.clear();
        cachedId_.clear();
        clearCreds("kitsu");
    }

    bool signIn(const std::string& user, const std::string& password,
                std::string& error) override {
        const json body{{"grant_type", "password"}, {"username", user}, {"password", password}};
        const auto res = http::postJson("https://kitsu.app/api/oauth/token", body.dump(), 20);
        if (!res.ok) {
            error = res.status == 401 ? "Kitsu rejected that username or password."
                                      : "Could not reach Kitsu.";
            return false;
        }
        try {
            const json j = json::parse(res.body);
            const std::string token = j.value("access_token", "");
            if (token.empty()) {
                error = "Kitsu did not return a token.";
                return false;
            }
            setCreds("kitsu", json{{"access_token", token},
                                   {"refresh_token", j.value("refresh_token", "")}});
            return true;
        } catch (const std::exception&) {
            error = "Kitsu sent something unreadable.";
            return false;
        }
    }

    std::vector<Entry> lists(bool* ok) override {
        if (ok) *ok = true;
        return {};
    }

    bool update(const MediaRef& what, int progress, const std::string& status) override {
        if (what.malId <= 0) return false;
        const std::string token = str(creds("kitsu"), "access_token");
        if (token.empty()) return false;

        Account me = account(false);
        if (!me.linked || me.id.empty()) return false;

        // Kitsu speaks its own ids, so the MAL id has to be translated first.
        const std::string kitsuId = kitsuIdForMal(what.malId);
        if (kitsuId.empty()) return false;

        // Update the existing library entry if there is one, create it if not.
        const std::string existing = libraryEntryId(me.id, kitsuId, token);
        const json attributes{{"progress", progress}, {"status", kitsuStatus(status)}};

        if (!existing.empty()) {
            const json body{{"data", {{"id", existing},
                                      {"type", "libraryEntries"},
                                      {"attributes", attributes}}}};
            return http::patchJson("https://kitsu.app/api/edge/library-entries/" + existing,
                                   body.dump(), 20, token)
                .ok;
        }

        const json body{
            {"data",
             {{"type", "libraryEntries"},
              {"attributes", attributes},
              {"relationships",
               {{"user", {{"data", {{"id", me.id}, {"type", "users"}}}}},
                {"anime", {{"data", {{"id", kitsuId}, {"type", "anime"}}}}}}}}}};
        return http::postJson("https://kitsu.app/api/edge/library-entries", body.dump(), 20, token)
            .ok;
    }

  private:
    static std::string kitsuStatus(const std::string& s) {
        if (s == "COMPLETED") return "completed";
        if (s == "PAUSED") return "on_hold";
        if (s == "DROPPED") return "dropped";
        if (s == "PLANNING") return "planned";
        return "current";
    }

    // Kitsu publishes a mapping table from other sites' ids to its own.
    static std::string kitsuIdForMal(int malId) {
        const auto res = http::get(
            "https://kitsu.app/api/edge/mappings?filter[externalSite]=myanimelist/anime"
            "&filter[externalId]=" +
            std::to_string(malId) + "&include=item", 15);
        if (!res.ok) return {};
        try {
            const json j = json::parse(res.body);
            if (j.contains("included") && j["included"].is_array() && !j["included"].empty()) {
                return j["included"][0].value("id", "");
            }
        } catch (const std::exception&) {
        }
        return {};
    }

    static std::string libraryEntryId(const std::string& userId, const std::string& animeId,
                                      const std::string& token) {
        const auto res = http::get("https://kitsu.app/api/edge/library-entries?filter[userId]=" +
                                       userId + "&filter[animeId]=" + animeId,
                                   15, token);
        if (!res.ok) return {};
        try {
            const json j = json::parse(res.body);
            if (j.contains("data") && j["data"].is_array() && !j["data"].empty()) {
                return j["data"][0].value("id", "");
            }
        } catch (const std::exception&) {
        }
        return {};
    }

    std::string cachedName_;
    std::string cachedId_;
};

MyAnimeList g_mal;
Simkl g_simkl;
Kitsu g_kitsu;

}  // namespace

// Handed to the registry in trackers.cpp.
Service* malService() { return &g_mal; }
Service* simklService() { return &g_simkl; }
Service* kitsuService() { return &g_kitsu; }

}  // namespace tsuzuki::tracker
