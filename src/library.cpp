#include "library.hpp"

#include "track.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace tsuzuki::library {
namespace {

using nlohmann::json;

// A write that has not reached AniList yet.
struct PendingUpdate {
    int mediaId = 0;
    int progress = 0;
    std::string status;  // CURRENT / COMPLETED
    long long queuedAt = 0;
    int attempts = 0;
    long long nextAttemptAt = 0;
    std::string lastError;
    bool parked = false;  // AniList rejected it outright; retrying will not help
};

std::mutex g_mutex;
std::map<std::string, EpisodeProgress> g_episodes;
std::map<int, CachedMedia> g_media;
std::vector<PendingUpdate> g_queue;

long long g_lastSyncAt = 0;
std::string g_lastError;
std::atomic<bool> g_syncing{false};
std::atomic<bool> g_running{false};
std::atomic<bool> g_pullRequested{false};
std::thread g_worker;
std::condition_variable g_wake;
std::mutex g_wakeMutex;
bool g_loaded = false;
bool g_dirty = false;

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string storeDir() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki" : std::string(".");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string storePath() { return storeDir() + "\\library.json"; }

// Written to a sibling file and renamed over the real one. A half-written
// library.json read back at the next launch would lose the lot.
void saveLocked() {
    json out;
    out["version"] = 1;

    json eps = json::object();
    for (const auto& [key, p] : g_episodes) {
        eps[key] = {{"anilistId", p.anilistId},
                    {"episode", p.episode},
                    {"currentTime", p.currentTime},
                    {"duration", p.duration},
                    {"completed", p.completed},
                    {"lastWatchedAt", p.lastWatchedAt},
                    {"updatedAt", p.updatedAt},
                    {"title", p.title},
                    {"cover", p.cover},
                    {"magnet", p.magnet},
                    {"file", p.file},
                    {"infoHash", p.infoHash},
                    {"fileIndex", p.fileIndex}};
    }
    out["episodes"] = eps;

    json media = json::object();
    for (const auto& [id, m] : g_media) {
        media[std::to_string(id)] = {{"mediaId", m.mediaId},   {"progress", m.progress},
                                     {"episodes", m.episodes}, {"nextEpisode", m.nextEpisode},
                                     {"status", m.status},     {"title", m.title},
                                     {"cover", m.cover},       {"color", m.color},
                                     {"airing", m.airing},     {"updatedAt", m.updatedAt},
                                     {"localChangeAt", m.localChangeAt}};
    }
    out["media"] = media;

    json queue = json::array();
    for (const auto& q : g_queue) {
        queue.push_back({{"mediaId", q.mediaId},
                         {"progress", q.progress},
                         {"status", q.status},
                         {"queuedAt", q.queuedAt},
                         {"attempts", q.attempts},
                         {"nextAttemptAt", q.nextAttemptAt},
                         {"lastError", q.lastError},
                         {"parked", q.parked}});
    }
    out["queue"] = queue;
    out["sync"] = {{"lastSyncAt", g_lastSyncAt}, {"lastError", g_lastError}};

    const std::string path = storePath();
    const std::string tmp = path + ".tmp";

    // Every failure below used to return quietly and then clear the dirty
    // flag anyway, so a database that could not be written was
    // indistinguishable from one that had been. It now says what went wrong
    // and stays dirty, so the next save - or the flush on the way out - tries
    // again instead of dropping the change.
    std::string trouble;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            trouble = "could not open " + tmp;
        } else {
            try {
                f << out.dump(2) << "\n";
            } catch (const std::exception& ex) {
                // dump throws on a string that is not valid UTF-8, which an
                // odd release name can produce.
                trouble = std::string("could not serialise: ") + ex.what();
            }
            if (trouble.empty() && !f) trouble = "write failed";
        }
    }

    if (trouble.empty()) {
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            // Renaming onto an open file can fail; replacing it outright is
            // the fallback, and only then is the change genuinely lost.
            std::error_code ec2;
            std::filesystem::remove(path, ec2);
            std::filesystem::rename(tmp, path, ec);
            if (ec) trouble = "could not replace " + path + ": " + ec.message();
        }
    }

    if (!trouble.empty()) {
        g_lastError = "Could not save the watch database - " + trouble;
        return;  // still dirty, so this will be retried
    }
    g_dirty = false;
}

std::string str(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return {};
    return j[key].get<std::string>();
}
long long num(const json& j, const char* key, long long fallback = 0) {
    if (!j.contains(key) || !j[key].is_number()) return fallback;
    return j[key].get<long long>();
}
double dbl(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return 0;
    return j[key].get<double>();
}
bool flag(const json& j, const char* key) {
    return j.contains(key) && j[key].is_boolean() && j[key].get<bool>();
}

void loadLocked() {
    if (g_loaded) return;
    g_loaded = true;

    std::ifstream in(storePath());
    if (!in) {
        // First run on this build: carry over the old flat progress store so
        // nobody loses their place upgrading.
        std::ifstream old(storeDir() + "\\progress.json");
        if (!old) return;
        try {
            json j;
            old >> j;
            if (!j.is_object()) return;
            for (const auto& [key, v] : j.items()) {
                if (!v.is_object()) continue;
                EpisodeProgress p;
                p.known = true;
                p.episode = static_cast<int>(num(v, "episode"));
                p.currentTime = dbl(v, "seconds");
                p.duration = dbl(v, "duration");
                p.title = str(v, "title");
                p.updatedAt = num(v, "updatedAt") * 1000;
                p.lastWatchedAt = p.updatedAt;
                if (key.rfind("al:", 0) == 0) {
                    p.anilistId = std::atoi(key.c_str() + 3);
                }
                g_episodes[key] = p;
            }
            g_dirty = true;
        } catch (const std::exception&) {
        }
        return;
    }

    try {
        json j;
        in >> j;
        if (!j.is_object()) return;

        if (j.contains("episodes") && j["episodes"].is_object()) {
            for (const auto& [key, v] : j["episodes"].items()) {
                if (!v.is_object()) continue;
                EpisodeProgress p;
                p.known = true;
                p.anilistId = static_cast<int>(num(v, "anilistId"));
                p.episode = static_cast<int>(num(v, "episode"));
                p.currentTime = dbl(v, "currentTime");
                p.duration = dbl(v, "duration");
                p.completed = flag(v, "completed");
                p.lastWatchedAt = num(v, "lastWatchedAt");
                p.updatedAt = num(v, "updatedAt");
                p.title = str(v, "title");
                p.cover = str(v, "cover");
                p.magnet = str(v, "magnet");
                p.file = str(v, "file");
                p.infoHash = str(v, "infoHash");
                p.fileIndex = static_cast<int>(num(v, "fileIndex", -1));
                g_episodes[key] = p;
            }
        }

        if (j.contains("media") && j["media"].is_object()) {
            for (const auto& [id, v] : j["media"].items()) {
                if (!v.is_object()) continue;
                CachedMedia m;
                m.mediaId = static_cast<int>(num(v, "mediaId"));
                if (m.mediaId <= 0) m.mediaId = std::atoi(id.c_str());
                m.progress = static_cast<int>(num(v, "progress"));
                m.episodes = static_cast<int>(num(v, "episodes"));
                m.nextEpisode = static_cast<int>(num(v, "nextEpisode", 1));
                m.status = str(v, "status");
                m.title = str(v, "title");
                m.cover = str(v, "cover");
                m.color = str(v, "color");
                m.airing = str(v, "airing");
                m.updatedAt = num(v, "updatedAt");
                m.localChangeAt = num(v, "localChangeAt");
                g_media[m.mediaId] = m;
            }
        }

        if (j.contains("queue") && j["queue"].is_array()) {
            for (const auto& v : j["queue"]) {
                if (!v.is_object()) continue;
                PendingUpdate q;
                q.mediaId = static_cast<int>(num(v, "mediaId"));
                q.progress = static_cast<int>(num(v, "progress"));
                q.status = str(v, "status");
                q.queuedAt = num(v, "queuedAt");
                q.attempts = static_cast<int>(num(v, "attempts"));
                q.nextAttemptAt = num(v, "nextAttemptAt");
                q.lastError = str(v, "lastError");
                q.parked = flag(v, "parked");
                // progress 0 is a legitimate status-only write, not a broken row
                if (q.mediaId > 0) g_queue.push_back(q);
            }
        }

        if (j.contains("sync") && j["sync"].is_object()) {
            g_lastSyncAt = num(j["sync"], "lastSyncAt");
            g_lastError = str(j["sync"], "lastError");
        }
    } catch (const std::exception&) {
        // A corrupt database costs the resume points, not the launch.
    }
}

void nudgeWorker() { g_wake.notify_all(); }

// 2s, 4s, 8s... capped at five minutes. Long enough not to hammer AniList
// through an outage, short enough that coming back online feels immediate.
long long backoffMs(int attempts) {
    const int shift = attempts > 8 ? 8 : attempts;
    long long ms = 2000LL << (shift - 1);
    return ms > 300000LL ? 300000LL : ms;
}

}  // namespace

std::string keyFor(int anilistId, int episode, const std::string& infoHash, int fileIndex) {
    if (anilistId > 0 && episode > 0) {
        return "al:" + std::to_string(anilistId) + ":" + std::to_string(episode);
    }
    return "t:" + infoHash + ":" + std::to_string(fileIndex);
}

EpisodeProgress get(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();
    const auto it = g_episodes.find(key);
    return it == g_episodes.end() ? EpisodeProgress{} : it->second;
}

void put(const EpisodeProgress& in) {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();

    EpisodeProgress p = in;
    p.known = true;
    if (p.updatedAt == 0) p.updatedAt = nowMs();
    if (p.lastWatchedAt == 0) p.lastWatchedAt = p.updatedAt;

    const std::string key = keyFor(p.anilistId, p.episode, p.infoHash, p.fileIndex);
    const auto it = g_episodes.find(key);
    if (it != g_episodes.end()) {
        // A write that left before one already stored must not land after it.
        if (p.updatedAt < it->second.updatedAt) return;
        // Once an episode is finished it stays finished, even if a late
        // checkpoint from earlier in the file arrives afterwards.
        if (it->second.completed) p.completed = true;
        // Keep details the newer write did not carry.
        if (p.cover.empty()) p.cover = it->second.cover;
        if (p.magnet.empty()) p.magnet = it->second.magnet;
        if (p.title.empty()) p.title = it->second.title;
        if (p.file.empty()) p.file = it->second.file;
    }

    g_episodes[key] = p;
    g_dirty = true;
    saveLocked();
}

void forget(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();
    if (g_episodes.erase(key) > 0) saveLocked();
}

void forgetAllInProgress() {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();

    bool changed = false;
    for (auto it = g_episodes.begin(); it != g_episodes.end();) {
        if (it->second.completed) {
            ++it;  // finished episodes are history, not a resume point
        } else {
            it = g_episodes.erase(it);
            changed = true;
        }
    }
    if (changed) saveLocked();
}

bool worthResuming(const EpisodeProgress& p) {
    if (!p.known || p.duration <= 0) return false;
    if (p.completed) return false;
    if (p.currentTime < 30) return false;                 // barely started
    if (p.currentTime > p.duration * 0.92) return false;  // effectively finished
    return true;
}

std::vector<EpisodeProgress> continueWatching(int limit) {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();

    std::vector<EpisodeProgress> out;
    for (const auto& [key, p] : g_episodes) {
        if (p.completed || p.duration <= 0) continue;
        if (p.currentTime < 30) continue;
        if (p.currentTime > p.duration * 0.92) continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const EpisodeProgress& a, const EpisodeProgress& b) {
        return a.lastWatchedAt > b.lastWatchedAt;
    });
    if (limit > 0 && static_cast<int>(out.size()) > limit) out.resize(limit);
    return out;
}

std::vector<CachedMedia> cachedList() {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();
    std::vector<CachedMedia> out;
    out.reserve(g_media.size());
    for (const auto& [id, m] : g_media) out.push_back(m);
    std::sort(out.begin(), out.end(),
              [](const CachedMedia& a, const CachedMedia& b) { return a.updatedAt > b.updatedAt; });
    return out;
}

CachedMedia cachedMedia(int mediaId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();
    const auto it = g_media.find(mediaId);
    return it == g_media.end() ? CachedMedia{} : it->second;
}

std::string SyncStatus::label() const {
    switch (state) {
        case SyncState::Syncing: return "Syncing...";
        case SyncState::Retrying: return "Sync failed - retrying";
        case SyncState::Offline: return "Offline - " + std::to_string(pending) + " queued";
        case SyncState::NotLinked: return "Not linked";
        case SyncState::Idle: break;
    }
    return pending > 0 ? "Syncing..." : "Synced";
}

SyncStatus syncStatus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();

    SyncStatus s;
    s.lastSyncAt = g_lastSyncAt;
    s.lastError = g_lastError;
    s.pending = 0;
    bool retrying = false;
    for (const auto& q : g_queue) {
        if (q.parked) continue;
        ++s.pending;
        if (q.attempts > 0) retrying = true;
    }

    if (!track::linked()) {
        s.state = SyncState::NotLinked;
    } else if (g_syncing) {
        s.state = SyncState::Syncing;
    } else if (retrying) {
        s.state = SyncState::Retrying;
    } else {
        s.state = SyncState::Idle;
    }
    return s;
}

void markWatching(int mediaId) {
    if (mediaId <= 0) return;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        loadLocked();

        const auto it = g_media.find(mediaId);
        if (it != g_media.end() &&
            (it->second.status == "CURRENT" || it->second.status == "REPEATING" ||
             it->second.status == "COMPLETED")) {
            return;  // already where it should be
        }
        for (const auto& q : g_queue) {
            if (q.mediaId == mediaId && !q.parked) return;  // already queued
        }

        auto& m = g_media[mediaId];
        m.mediaId = mediaId;
        m.status = "CURRENT";
        m.updatedAt = nowMs();
        m.localChangeAt = nowMs();

        PendingUpdate q;
        q.mediaId = mediaId;
        q.progress = 0;  // status only
        q.status = "CURRENT";
        q.queuedAt = nowMs();
        g_queue.push_back(q);

        g_dirty = true;
        saveLocked();
    }
    nudgeWorker();
}

void recordWatched(int mediaId, int episode, int totalEpisodes) {
    if (mediaId <= 0 || episode <= 0) return;

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        loadLocked();

        // Finishing the last episode moves the entry to COMPLETED; anything
        // else leaves it CURRENT, matching what has actually been watched.
        const std::string status =
            (totalEpisodes > 0 && episode >= totalEpisodes) ? "COMPLETED" : "CURRENT";

        // The optimistic write: the cache reflects the new progress straight
        // away, so the interface can move on without waiting for the network.
        auto& m = g_media[mediaId];
        m.mediaId = mediaId;
        if (episode > m.progress) {
            m.progress = episode;
            m.updatedAt = nowMs();
        }
        m.localChangeAt = nowMs();
        if (m.episodes <= 0 && totalEpisodes > 0) m.episodes = totalEpisodes;
        m.nextEpisode = (m.episodes > 0 && m.progress >= m.episodes) ? m.progress : m.progress + 1;
        if (!status.empty()) m.status = status;

        // Collapse against anything already queued for this media. Replayed
        // completion events, or finishing two episodes back to back, produce
        // one write rather than several.
        bool merged = false;
        for (auto& q : g_queue) {
            if (q.mediaId != mediaId) continue;
            if (episode > q.progress) {
                q.progress = episode;
                q.status = status;
            }
            q.parked = false;
            q.nextAttemptAt = 0;
            merged = true;
            break;
        }
        if (!merged) {
            PendingUpdate q;
            q.mediaId = mediaId;
            q.progress = episode;
            q.status = status;
            q.queuedAt = nowMs();
            g_queue.push_back(q);
        }

        g_dirty = true;
        saveLocked();
    }

    nudgeWorker();
}

void refreshFromAniList() {
    g_pullRequested = true;
    nudgeWorker();
}

namespace {

// Merge an AniList pull into the cache. An entry we have queued a higher
// progress for keeps the local number: AniList simply has not been told yet,
// and taking its answer would roll the user backwards.
void mergePull(const std::vector<track::ListEntry>& entries) {
    std::lock_guard<std::mutex> lock(g_mutex);
    loadLocked();

    for (const auto& e : entries) {
        if (e.mediaId <= 0) continue;

        int queuedProgress = 0;
        for (const auto& q : g_queue) {
            if (q.mediaId == e.mediaId && !q.parked) queuedProgress = std::max(queuedProgress, q.progress);
        }

        auto& m = g_media[e.mediaId];
        const int localProgress = m.progress;
        m.mediaId = e.mediaId;
        m.episodes = e.episodes;
        m.title = e.title;
        m.cover = e.cover;
        m.color = e.color;
        m.airing = e.airing;
        m.status = e.status;
        m.progress = std::max({e.progress, queuedProgress, localProgress});
        m.nextEpisode = (m.episodes > 0 && m.progress >= m.episodes) ? m.progress : m.progress + 1;
        m.updatedAt = nowMs();
    }

    // A pull is the whole list, so anything missing from it has been removed
    // on the website. Without this the cache only ever grew: deleting a show
    // on AniList left it sitting in Continue Watching here forever.
    //
    // Two exceptions, both about writes AniList has not been told about yet.
    // Something still in the queue is legitimately absent from its answer,
    // and something written seconds ago may have missed this snapshot; both
    // would come straight back on the next pull, so dropping them would only
    // make the row flicker.
    {
        std::set<int> present;
        for (const auto& e : entries) present.insert(e.mediaId);

        const long long now = nowMs();
        for (auto it = g_media.begin(); it != g_media.end();) {
            if (present.count(it->first) > 0) {
                ++it;
                continue;
            }
            bool queued = false;
            for (const auto& q : g_queue) {
                if (q.mediaId == it->first && !q.parked) queued = true;
            }
            // Deliberately localChangeAt, not updatedAt: every pull refreshes
            // updatedAt, so using it meant the guard protected everything for a
            // minute after any pull and deletions took minutes to show up.
            const bool justWritten = now - it->second.localChangeAt < 60000;
            if (queued || justWritten) {
                ++it;
            } else {
                it = g_media.erase(it);
            }
        }
    }

    g_lastSyncAt = nowMs();
    g_dirty = true;
    saveLocked();
}

// One pass over the queue. Returns true if anything is still waiting.
bool drainQueue() {
    std::vector<PendingUpdate> due;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        loadLocked();
        const long long now = nowMs();
        for (const auto& q : g_queue) {
            if (q.parked) continue;
            if (q.nextAttemptAt > now) continue;
            due.push_back(q);
        }
    }
    if (due.empty()) return false;

    for (const auto& q : due) {
        if (!g_running) return true;

        const bool ok = track::updateEntry(q.mediaId, q.progress, q.status);

        std::lock_guard<std::mutex> lock(g_mutex);
        for (auto it = g_queue.begin(); it != g_queue.end(); ++it) {
            if (it->mediaId != q.mediaId) continue;

            if (ok) {
                // Something newer may have been queued while this was in
                // flight; only clear the entry if it is still the same write.
                if (it->progress <= q.progress) {
                    g_queue.erase(it);
                    g_lastError.clear();
                }
            } else {
                ++it->attempts;
                // A refusal for being too chatty is not the same as a failed
                // write, and retrying it on the usual two second curve is how
                // a rate limit turns into a longer rate limit.
                const long long wait =
                    (std::max)(backoffMs(it->attempts), static_cast<long long>(60000));
                it->nextAttemptAt = nowMs() + wait;
                it->lastError = "AniList did not accept the update";
                g_lastError = it->lastError;
                // Twenty attempts is well over an hour of backoff. Past that
                // this is not a blip, so stop retrying and keep it visible
                // rather than looping forever.
                if (it->attempts >= 20) it->parked = true;
            }
            break;
        }
        g_dirty = true;
        saveLocked();
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& q : g_queue) {
        if (!q.parked) return true;
    }
    return false;
}

void workerLoop() {
    // Startup pull, so a fresh launch reconciles with AniList without anyone
    // asking it to.
    g_pullRequested = true;
    long long lastPull = 0;

    while (g_running) {
        const bool linked = track::linked();

        if (linked) {
            const long long now = nowMs();
            // Re-pull every five minutes, and whenever something asks.
            // Two minutes rather than five: the point is for this to track
            // the website closely enough that the two never visibly disagree.
            if (g_pullRequested || now - lastPull > 2 * 60 * 1000) {
                g_pullRequested = false;
                lastPull = now;
                g_syncing = true;
                bool ok = false;
                const auto entries = track::lists(&ok);
                if (ok) {
                    // Cleared before the merge, not after: mergePull is what
                    // writes the file, so clearing afterwards left the old
                    // message on disk until something else happened to save.
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        g_lastError.clear();
                    }
                    // Including when it came back empty: that is a real answer
                    // about an account with nothing on its list yet, not a
                    // failure to reach AniList.
                    mergePull(entries);
                } else {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_lastError = "Could not read your AniList library";
                }
                g_syncing = false;
            }

            g_syncing = true;
            drainQueue();
            g_syncing = false;
        }

        std::unique_lock<std::mutex> lock(g_wakeMutex);
        g_wake.wait_for(lock, std::chrono::seconds(linked ? 5 : 20));
    }
}

}  // namespace

void start() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        loadLocked();
    }
    if (g_running.exchange(true)) return;
    g_worker = std::thread(workerLoop);
}

void stop() {
    if (!g_running.exchange(false)) return;
    nudgeWorker();
    if (g_worker.joinable()) g_worker.join();
    flush();
}

void flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_loaded && g_dirty) saveLocked();
}

}  // namespace tsuzuki::library
