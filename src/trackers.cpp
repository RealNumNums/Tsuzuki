// The registry, and AniList behind the shared interface.
//
// AniList keeps its own module: it is not only a tracker but the source of
// artwork, titles, episode lists and the trending shelves, so track.cpp stays
// where it is and this wraps it.

#include "tracker.hpp"

#include "track.hpp"

#include <algorithm>

namespace tsuzuki::tracker {
namespace {

class AniList final : public Service {
  public:
    const char* id() const override { return "anilist"; }
    const char* name() const override { return "AniList"; }
    AuthKind authKind() const override { return AuthKind::HostedLogin; }

    void load() override { track::load(); }
    bool linked() const override { return track::linked(); }

    Account account(bool force) override {
        const track::Account a = track::account(force);
        Account out;
        out.linked = a.linked;
        out.id = std::to_string(a.id);
        out.name = a.name;
        out.avatar = a.avatar;
        return out;
    }

    void logout() override { track::logout(); }

    std::string authorizeUrl() const override {
        return track::implicitAuthorizeUrl(track::resolveClientId(""));
    }

    bool acceptRedirect(const std::string& url) override {
        // AniList returns the token in the fragment, so it never has to reach
        // the redirect target - which is why the registered URL can be
        // anything at all.
        const auto at = url.find("access_token=");
        if (at == std::string::npos) return false;
        std::string rest = url.substr(at + 13);
        const auto end = rest.find_first_of("&#");
        if (end != std::string::npos) rest = rest.substr(0, end);
        if (rest.empty()) return false;
        track::setToken(rest);
        return true;
    }

    std::vector<Entry> lists(bool* ok) override {
        std::vector<Entry> out;
        for (const auto& e : track::lists(ok)) {
            Entry x;
            x.mediaId = e.mediaId;
            x.progress = e.progress;
            x.episodes = e.episodes;
            x.nextEpisode = e.nextEpisode;
            x.status = e.status;
            x.title = e.title;
            x.cover = e.cover;
            x.color = e.color;
            x.airing = e.airing;
            out.push_back(std::move(x));
        }
        return out;
    }

    bool update(const MediaRef& what, int progress, const std::string& status) override {
        if (what.anilistId <= 0) return false;
        return track::updateEntry(what.anilistId, progress, status);
    }
};

AniList g_anilist;

}  // namespace

// Defined in trackers_ext.cpp, which owns the three that are not AniList.
Service* malService();
Service* simklService();
Service* kitsuService();

namespace {

// Order matters only in that it is stable; AniList first because it is primary.
std::vector<Service*> g_services;

}  // namespace

std::vector<Service*> all() {
    if (g_services.empty()) {
        // Only AniList for now, by choice rather than because the others do
        // not work - MyAnimeList links and syncs, Simkl and Kitsu are written
        // and waiting. Put them back by restoring the line below; their code,
        // their settings rows and any stored tokens are all still here, so
        // nothing has to be set up again.
        //
        //   g_services = {&g_anilist, malService(), simklService(), kitsuService()};
        g_services = {&g_anilist};
    }
    return g_services;
}

Service* byId(const std::string& id) {
    auto services = all();
    const auto it = std::find_if(services.begin(), services.end(),
                                 [&](Service* s) { return id == s->id(); });
    return it == services.end() ? nullptr : *it;
}

Service* primary() { return &g_anilist; }

void loadAll() {
    for (Service* s : all()) s->load();
}

}  // namespace tsuzuki::tracker
