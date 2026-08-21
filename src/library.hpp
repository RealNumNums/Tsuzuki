#pragma once

#include <string>
#include <vector>

namespace tsuzuki::library {

// The local watch database, and the queue that carries changes to AniList.
//
// Two rules shape everything here:
//
//   1. Local progress is never lost because AniList is unreachable. Writes land
//      on disk first and are queued for AniList second, so pulling the network
//      cable costs nothing.
//   2. Nothing older ever overwrites something newer. Every record carries an
//      updatedAt, and both the AniList pull and the resume path compare it
//      before replacing what is already stored.
//
// Replaces the flat progress.json store, which keyed on episode but had no
// notion of a pending write, a version, or a completion that had already been
// reported.

// ---------------------------------------------------------------- episodes

struct EpisodeProgress {
    bool known = false;

    int anilistId = 0;
    int episode = 0;
    double currentTime = 0;  // seconds into the episode
    double duration = 0;     // total runtime, 0 when nothing has reported one
    bool completed = false;

    long long lastWatchedAt = 0;  // when the user last had it open
    long long updatedAt = 0;      // version stamp, for conflict resolution

    // Enough to reopen the episode straight from the home screen.
    std::string title;
    std::string cover;
    std::string magnet;
    std::string file;
    std::string infoHash;
    int fileIndex = -1;

    double percent() const {
        if (duration <= 0) return 0;
        const double p = currentTime / duration * 100.0;
        return p < 0 ? 0 : (p > 100 ? 100 : p);
    }
    double remaining() const { return duration > currentTime ? duration - currentTime : 0; }
};

// AniList id when there is one, otherwise the torrent's infohash and file
// index, so plain magnets resume as well.
std::string keyFor(int anilistId, int episode, const std::string& infoHash, int fileIndex);

EpisodeProgress get(const std::string& key);

// Writes through to disk. Ignored if `p.updatedAt` is older than what is
// already stored, so a delayed write cannot rewind someone's position.
void put(const EpisodeProgress& p);

void forget(const std::string& key);

// Far enough in to be worth resuming, and not so close to the end that
// resuming would only replay the credits.
bool worthResuming(const EpisodeProgress& p);

// Everything still part-watched, most recent first, for Continue Watching.
std::vector<EpisodeProgress> continueWatching(int limit);

// ------------------------------------------------------------ anilist cache

struct CachedMedia {
    int mediaId = 0;
    int progress = 0;
    int episodes = 0;
    int nextEpisode = 1;
    std::string status;
    std::string title;
    std::string cover;
    std::string color;
    std::string airing;
    long long updatedAt = 0;
};

// Served immediately at startup so the home screen never waits on the network.
std::vector<CachedMedia> cachedList();
CachedMedia cachedMedia(int mediaId);

// ------------------------------------------------------------------- sync

enum class SyncState { Idle, Syncing, Retrying, Offline, NotLinked };

struct SyncStatus {
    SyncState state = SyncState::NotLinked;
    long long lastSyncAt = 0;
    int pending = 0;
    std::string lastError;
    std::string label() const;  // "Synced", "Syncing...", "Sync failed - retrying"
};

SyncStatus syncStatus();

// Queue an episode as watched. Collapses against anything already queued for
// the same media, keeping the higher progress, so replayed completion events
// cannot produce duplicate AniList writes.
void recordWatched(int mediaId, int episode, int totalEpisodes);

// Pull AniList into the cache. Runs on its own thread; call from startup or a
// manual refresh. Newer local progress is never overwritten by an older pull.
void refreshFromAniList();

// Start/stop the background worker that drains the queue and re-pulls
// periodically. start() also loads the database from disk.
void start();
void stop();

// Force everything still in memory out to disk. Called when the app closes.
void flush();

}  // namespace tsuzuki::library
