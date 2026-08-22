#pragma once

// One shape for every list-tracking service.
//
// The sync layer only ever needed five things from AniList - is it linked, who
// is it, read the list, write an episode, forget the token - so that is the
// whole interface. What differs between services is not what they do but how
// they let you in, and no two of them agree:
//
//   AniList       a consent page, token handed back in the redirect fragment
//   MyAnimeList   OAuth with PKCE, token exchanged for a code
//   Simkl         a device code: show a PIN, the user approves it on the site
//   Kitsu         no consent page at all, only a password grant
//
// So authentication is the one place the interface has to be lumpy, and it is
// deliberately lumpy in the open - a service declares which kind it is and the
// settings screen asks for the right thing, rather than every caller pretending
// they are all OAuth and special-casing the ones that are not.
//
// Identity across services is the MyAnimeList id. AniList publishes it for
// every entry, Simkl and Kitsu both accept it, and MAL obviously does; without
// it there is no way to say "this show" to more than one service.

#include <string>
#include <vector>

namespace tsuzuki::tracker {

// Which show, in whatever terms a given service understands.
struct MediaRef {
    int anilistId = 0;
    int malId = 0;
};

// A show as one service sees it. Statuses are normalised to AniList's
// vocabulary because the rest of the program already speaks it.
struct Entry {
    int mediaId = 0;  // the service's own id
    int malId = 0;
    int progress = 0;
    int episodes = 0;
    int nextEpisode = 1;
    std::string status;  // CURRENT / COMPLETED / PAUSED / DROPPED / PLANNING
    std::string title;
    std::string cover;
    std::string color;
    std::string airing;
};

struct Account {
    bool linked = false;
    std::string id;
    std::string name;
    std::string avatar;
};

enum class AuthKind {
    HostedLogin,  // open a page in a window we own and read the token back
    DeviceCode,   // show a code; the user approves it on the service's site
    Password,     // no consent page exists
};

// What a device-code service wants shown while it waits.
struct DeviceCode {
    bool ok = false;
    std::string userCode;         // what to type in
    std::string verificationUrl;  // where to type it
    int intervalSeconds = 5;      // how often to ask whether it has been approved
    std::string error;
};

class Service {
  public:
    virtual ~Service() = default;

    virtual const char* id() const = 0;    // "anilist", stable, used in settings
    virtual const char* name() const = 0;  // "AniList", shown to people
    virtual AuthKind authKind() const = 0;

    // False when the service needs a client id nobody has supplied yet. Such a
    // service is listed but cannot be linked, and says why.
    virtual bool configured() const { return true; }
    virtual std::string configHint() const { return {}; }

    virtual void load() = 0;  // read any stored token at startup
    virtual bool linked() const = 0;
    virtual Account account(bool force = false) = 0;
    virtual void logout() = 0;

    // ---- linking, per kind ------------------------------------------------
    virtual std::string authorizeUrl() const { return {}; }
    // Given the URL a hosted login window is navigating to, take a token out of
    // it if there is one. True when linking is complete.
    virtual bool acceptRedirect(const std::string& url) { return false; }

    virtual DeviceCode beginDeviceCode() { return {}; }
    // True once approved; false while still waiting. `error` is set when the
    // attempt has failed for good.
    virtual bool pollDeviceCode(std::string& error) { return false; }

    virtual bool signIn(const std::string& user, const std::string& password,
                        std::string& error) {
        error = "This service does not use a password.";
        return false;
    }

    // ---- syncing ----------------------------------------------------------
    // `ok` separates "the request failed" from "the list is empty".
    virtual std::vector<Entry> lists(bool* ok) = 0;
    virtual bool update(const MediaRef&, int progress, const std::string& status) = 0;
};

// Every service, linked or not, in a stable order.
std::vector<Service*> all();
Service* byId(const std::string& id);

// The one whose list drives the home screen. AniList, because it is the only
// one that also answers "what is trending" and carries the cross-service id.
Service* primary();

void loadAll();

}  // namespace tsuzuki::tracker
