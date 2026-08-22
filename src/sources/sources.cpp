#include "source.hpp"

#include <nlohmann/json.hpp>
#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <sstream>

#include "../http.hpp"

namespace tsuzuki::sources {
namespace {

using nlohmann::json;

// Trackers appended when a source gives us an infohash but no magnet.
constexpr const char* kTrackers[] = {
    "http://nyaa.tracker.wf:7777/announce",
    "udp://tracker.opentrackr.org:1337/announce",
    "udp://open.stealth.si:80/announce",
    "udp://exodus.desync.com:6969/announce",
};

std::string magnetFor(const std::string& infoHash, const std::string& name) {
    std::string m = "magnet:?xt=urn:btih:" + infoHash;
    if (!name.empty()) m += "&dn=" + http::urlEncode(name);
    for (const char* tr : kTrackers) m += "&tr=" + http::urlEncode(tr);
    return m;
}

// Nyaa reports sizes as human strings ("1.4 GiB").
std::int64_t parseHumanSize(const std::string& s) {
    double value = 0;
    char unit[8] = {0};
    if (std::sscanf(s.c_str(), "%lf %7s", &value, unit) < 1) return 0;
    std::string u(unit);
    std::transform(u.begin(), u.end(), u.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    double mult = 1;
    if (u.rfind("ki", 0) == 0 || u.rfind("kb", 0) == 0) mult = 1024.0;
    else if (u.rfind("mi", 0) == 0 || u.rfind("mb", 0) == 0) mult = 1024.0 * 1024;
    else if (u.rfind("gi", 0) == 0 || u.rfind("gb", 0) == 0) mult = 1024.0 * 1024 * 1024;
    else if (u.rfind("ti", 0) == 0 || u.rfind("tb", 0) == 0) mult = 1024.0 * 1024 * 1024 * 1024.0;
    return static_cast<std::int64_t>(value * mult);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Query text used against title-based indexes.
//
// Episode is normally left out: releases name episodes inconsistently, so we
// search broad and let the Anitomy pass in scan.cpp do the precise filtering.
// The exception is when the caller has asked for one specific episode, which
// happens after a broad search came back without it - indexes return their
// most-seeded results first, so for an airing show the early episodes fall off
// the end of the page and no amount of local filtering can bring them back.
std::string queryText(const Query& q) {
    std::string text = q.title;
    if (q.episode && *q.episode > 0) {
        const int ep = *q.episode;
        text += ep < 10 ? " 0" + std::to_string(ep) : " " + std::to_string(ep);
    }
    if (q.season && *q.season > 1) text += " S" + std::string(*q.season < 10 ? "0" : "") +
                                            std::to_string(*q.season);
    if (q.resolution) text += " " + std::to_string(*q.resolution) + "p";
    return text;
}

/* ------------------------------------------------------------------- Nyaa */

class Nyaa final : public Source {
public:
    std::string id() const override { return "nyaa-si"; }
    std::string name() const override { return "Nyaa"; }
    Accuracy accuracy() const override { return Accuracy::Medium; }

    std::vector<Result> search(const Query& q) override {
        std::vector<Result> out;
        // c=1_2 is "Anime - English-translated"; f=0 means no filter.
        const std::string url = "https://nyaa.si/?page=rss&c=1_2&f=0&q=" +
                                http::urlEncode(queryText(q));
        const auto res = http::get(url);
        if (!res.ok) return out;

        pugi::xml_document doc;
        if (!doc.load_string(res.body.c_str())) return out;

        for (const auto item : doc.select_nodes("/rss/channel/item")) {
            const auto n = item.node();
            Result r;
            r.title = n.child_value("title");
            r.link = n.child_value("guid");
            r.infoHash = lower(n.child_value("nyaa:infoHash"));
            r.seeders = std::atoi(n.child_value("nyaa:seeders"));
            r.leechers = std::atoi(n.child_value("nyaa:leechers"));
            r.downloads = std::atoll(n.child_value("nyaa:downloads"));
            r.size = parseHumanSize(n.child_value("nyaa:size"));
            r.date = n.child_value("pubDate");
            r.sourceId = id();
            // Nyaa flags vetted uploaders; treat those as higher confidence.
            r.accuracy = std::string(n.child_value("nyaa:trusted")) == "Yes"
                             ? Accuracy::High
                             : Accuracy::Medium;
            if (r.infoHash.empty()) continue;
            r.magnet = magnetFor(r.infoHash, r.title);
            out.push_back(std::move(r));
        }
        return out;
    }
};

/* ------------------------------------------------------------- AnimeTosho */

class AnimeTosho final : public Source {
public:
    std::string id() const override { return "animetosho"; }
    std::string name() const override { return "AnimeTosho"; }
    Accuracy accuracy() const override { return Accuracy::High; }

    std::vector<Result> search(const Query& q) override {
        std::vector<Result> out;
        const std::string url = "https://feed.animetosho.org/json?only_tor=1&q=" +
                                http::urlEncode(queryText(q));
        const auto res = http::get(url);
        if (!res.ok) return out;

        json j;
        try {
            j = json::parse(res.body);
        } catch (const std::exception&) {
            return out;
        }
        if (!j.is_array()) return out;

        for (const auto& e : j) {
            Result r;
            r.title = e.value("title", "");
            r.magnet = e.value("magnet_uri", "");
            r.infoHash = lower(e.value("info_hash", ""));
            r.link = e.value("link", "");
            r.seeders = e.value("seeders", 0);
            r.leechers = e.value("leechers", 0);
            r.downloads = e.value("torrent_downloaded_count", 0);
            r.size = e.value("total_size", (std::int64_t)0);
            r.accuracy = Accuracy::High;
            r.sourceId = id();
            if (r.infoHash.empty() && r.magnet.empty()) continue;
            if (r.magnet.empty()) r.magnet = magnetFor(r.infoHash, r.title);
            out.push_back(std::move(r));
        }
        return out;
    }
};

/* ------------------------------------------------------------- SubsPlease */

class SubsPlease final : public Source {
public:
    std::string id() const override { return "subsplease"; }
    std::string name() const override { return "SubsPlease"; }
    Accuracy accuracy() const override { return Accuracy::Medium; }

    std::vector<Result> search(const Query& q) override {
        std::vector<Result> out;
        const std::string url = "https://subsplease.org/api/?f=search&tz=UTC&s=" +
                                http::urlEncode(q.title);
        const auto res = http::get(url);
        if (!res.ok) return out;

        json j;
        try {
            j = json::parse(res.body);
        } catch (const std::exception&) {
            return out;
        }
        if (!j.is_object()) return out;

        for (const auto& [key, e] : j.items()) {
            if (!e.is_object()) continue;
            const std::string show = e.value("show", "");
            const std::string ep = e.value("episode", "");

            for (const auto& d : e.value("downloads", json::array())) {
                const std::string resolution = d.value("res", "");
                if (q.resolution && resolution != std::to_string(*q.resolution)) continue;

                Result r;
                // SubsPlease does not publish the full release name, so
                // reconstruct the one it actually uses on the torrent.
                r.title = "[SubsPlease] " + show + " - " + ep + " (" + resolution + "p)";
                r.magnet = d.value("magnet", "");
                r.infoHash = infoHashFromMagnet(r.magnet);
                r.date = e.value("release_date", "");
                r.accuracy = Accuracy::Medium;
                r.sourceId = id();
                // No seeder counts in this API; leave at 0 rather than invent.
                if (r.magnet.empty()) continue;
                out.push_back(std::move(r));
            }
        }
        return out;
    }

private:
    // SubsPlease magnets use base32 infohashes; keep the raw form for dedupe
    // rather than converting, since we only compare against ourselves here.
    static std::string infoHashFromMagnet(const std::string& magnet) {
        const auto pos = magnet.find("urn:btih:");
        if (pos == std::string::npos) return {};
        const auto start = pos + 9;
        const auto end = magnet.find('&', start);
        return lower(magnet.substr(start, end == std::string::npos ? std::string::npos
                                                                  : end - start));
    }
};

/* ----------------------------------------------------------------- SeaDex */

class SeaDex final : public Source {
public:
    std::string id() const override { return "seadex"; }
    std::string name() const override { return "SeaDex"; }
    Accuracy accuracy() const override { return Accuracy::High; }

    std::vector<Result> search(const Query& q) override {
        std::vector<Result> out;
        // SeaDex is a curated best-release list keyed by AniList ID. Without
        // one there is nothing sensible to ask it.
        if (!q.anilistId) return out;

        const std::string url =
            "https://releases.moe/api/collections/entries/records?expand=trs&filter=" +
            http::urlEncode("alID=" + std::to_string(*q.anilistId));
        const auto res = http::get(url);
        if (!res.ok) return out;

        json j;
        try {
            j = json::parse(res.body);
        } catch (const std::exception&) {
            return out;
        }

        for (const auto& entry : j.value("items", json::array())) {
            const auto trs = entry.value("expand", json::object()).value("trs", json::array());
            for (const auto& t : trs) {
                const std::string hash = lower(t.value("infoHash", ""));
                // SeaDex lists private-tracker releases too; those have no
                // usable infohash for us.
                if (hash.empty() || hash == "<redacted>") continue;
                if (q.exclusive && !t.value("isBest", false)) continue;

                Result r;
                const std::string group = t.value("releaseGroup", "");
                r.title = group.empty() ? t.value("url", "") : "[" + group + "] " + q.title;
                r.infoHash = hash;
                r.link = t.value("url", "");
                r.magnet = magnetFor(hash, r.title);
                r.accuracy = Accuracy::High;
                r.sourceId = id();
                r.curatedBest = t.value("isBest", false);

                for (const auto& f : t.value("files", json::array())) {
                    r.size += f.value("length", (std::int64_t)0);
                }
                out.push_back(std::move(r));
            }
        }
        return out;
    }
};

}  // namespace

std::shared_ptr<Source> makeNyaa() { return std::make_shared<Nyaa>(); }
std::shared_ptr<Source> makeAnimeTosho() { return std::make_shared<AnimeTosho>(); }
std::shared_ptr<Source> makeSubsPlease() { return std::make_shared<SubsPlease>(); }
std::shared_ptr<Source> makeSeaDex() { return std::make_shared<SeaDex>(); }

// nekoBT serves HTML with no JSON API, and AnimeTosho already carries a
// nekobt_id for cross-indexed releases, so it is intentionally not implemented.
std::shared_ptr<Source> makeNekoBT() { return nullptr; }

std::vector<Result> searchAll(const std::vector<std::shared_ptr<Source>>& sources,
                              const Query& q) {
    std::map<std::string, Result> byHash;

    for (const auto& src : sources) {
        if (!src) continue;
        std::vector<Result> results;
        try {
            results = src->search(q);
        } catch (const std::exception&) {
            continue;  // a broken source must not sink the whole search
        }

        for (auto& r : results) {
            if (r.infoHash.empty()) continue;
            auto it = byHash.find(r.infoHash);
            if (it == byHash.end()) {
                byHash.emplace(r.infoHash, std::move(r));
                continue;
            }
            // Merge duplicates: keep the longest title (most descriptive), the
            // best accuracy seen, and the highest peer counts reported.
            Result& kept = it->second;
            if (r.title.size() > kept.title.size()) kept.title = r.title;
            if (r.accuracy > kept.accuracy) kept.accuracy = r.accuracy;
            kept.curatedBest = kept.curatedBest || r.curatedBest;
            kept.seeders = std::max(kept.seeders, r.seeders);
            kept.leechers = std::max(kept.leechers, r.leechers);
            if (kept.size == 0) kept.size = r.size;
            if (kept.date.empty()) kept.date = r.date;
            if (kept.sourceId.find(r.sourceId) == std::string::npos) {
                kept.sourceId += "+" + r.sourceId;
            }
        }
    }

    std::vector<Result> out;
    out.reserve(byHash.size());
    for (auto& [_, r] : byHash) out.push_back(std::move(r));

    std::sort(out.begin(), out.end(), [](const Result& a, const Result& b) {
        // Curated picks first even with no peer counts, then accuracy, then
        // seeders. A SeaDex "best" entry is worth more than a popular re-encode.
        if (a.curatedBest != b.curatedBest) return a.curatedBest;
        if (a.accuracy != b.accuracy) return a.accuracy > b.accuracy;
        return a.seeders > b.seeders;
    });
    return out;
}

}  // namespace tsuzuki::sources
