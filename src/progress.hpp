#pragma once

#include <string>

namespace tsuzuki::progress {

// Where playback got to, so an episode resumes instead of restarting.
//
// Ported from hayase-app/interface src/lib/modules/watchProgress.ts, which
// keeps Record<mediaId, {episode, currentTime, safeduration}> - one entry per
// anime. That loses your place in episode 5 the moment you open episode 8, so
// this keys on the episode as well and remembers each one separately.

struct Position {
    bool known = false;
    int episode = 0;
    double seconds = 0;
    double duration = 0;
    std::string title;
    long long updatedAt = 0;
};

// AniList id when there is one, otherwise the torrent's infohash and file
// index, so plain magnets resume too.
std::string keyFor(int anilistId, int episode, const std::string& infoHash, int fileIndex);

void load();
Position get(const std::string& key);
void set(const std::string& key, const Position& p);
void clear(const std::string& key);

// Far enough in to be worth resuming, and not so close to the end that
// resuming would just replay the credits.
bool worthResuming(const Position& p);

}  // namespace tsuzuki::progress
