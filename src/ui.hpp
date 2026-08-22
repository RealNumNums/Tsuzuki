#pragma once

#include <string>
#include <vector>

#include <map>

#include "settings.hpp"

namespace tsuzuki::ui {

// Starts the same server on a background thread and returns once it is
// accepting connections, leaving the caller to supply its own window. Used by
// the native app build, which hosts the interface in a WebView2 control
// instead of handing it to a browser.
bool startBackground(int port, const std::string& savePath);

// Native app only. Renders video inside `hwnd` (via mpv --wid) rather than in
// a separate player window, and calls `hook` with true just before playback
// starts and false once it ends, so the host can swap the video surface in and
// out. The hook runs on a worker thread - post a message, do not touch UI
// state directly. Pass nullptr to go back to a standalone mpv window.
using PlaybackHook = void (*)(bool active);
void setVideoHost(void* hwnd, PlaybackHook hook);

// Removes the open torrent and its data. Call before the process exits -
// downloads are disposable, and leaving them behind is precisely the leak this
// project exists to avoid. Safe to call when nothing is open.
// Native app only. When set, linking opens a window the app controls instead
// of handing the user off to their browser, which is what lets the token be
// read straight out of the redirect.
using AuthHook = void (*)(const char* url);
void setAuthHook(AuthHook hook);

// Called by that window once it has the token.
void acceptToken(const std::string& token);

// Recently opened torrents, newest first. The native interface reads this
// directly rather than going back out through the loopback server.
struct HistoryItem {
    std::string magnet, file, torrent, show, cover;
    int anilistId = 0;
    int episode = 0;
    long long at = 0;
};
std::vector<HistoryItem> history();

// Remove one row from the recently-opened list, or empty it.
void forgetHistory(const std::string& magnet, const std::string& file);
void clearHistory();

// Settings, for the screen that edits them. applySettings writes to disk and
// pushes the parts the running session cares about (speed limit, connection
// cap, DoH resolver) straight into libtorrent.
// ---- searching -------------------------------------------------------
//
// Every one of these blocks on the network, so callers run them off the
// interface thread and hand the result back when it arrives.

struct Found {
    std::string title;      // release name, as published
    std::string magnet;
    std::string sourceId;
    std::string accuracy;
    long long size = 0;
    int seeders = 0;
    bool curatedBest = false;  // SeaDex says this is the encode to take
};

struct SearchOutcome {
    std::string error;
    std::string resolvedTitle;  // AniList's canonical title, when it matched
    int anilistId = 0;
    int episodes = 0;
    bool autoSelect = false;
    std::vector<Found> results;
};

SearchOutcome search(const std::string& query, int resolution);

// ---- opening a torrent ----------------------------------------------

struct OpenedFile {
    int index = 0;
    std::string name;
    std::string episodeLabel;  // "EP 5", "1-12", "??" or "--"
    std::string reason;        // why it was skipped, when it was
    long long size = 0;
    bool skipped = false;
    int episodeNumber = 0;     // 0 when unknown or a range
};

struct EpisodeMeta {
    std::string title;
    std::string thumb;
};

struct OpenOutcome {
    std::string error;
    std::string torrentName;

    std::string title, description, cover, banner, color;
    int episodes = 0, duration = 0, anilistId = 0;

    // The file matching the episode asked for. -1 with refused set means it
    // was missing or ambiguous - never a different file quietly substituted.
    int target = -1;
    bool refused = false;
    int wanted = 0;

    std::vector<OpenedFile> files;
    std::map<int, EpisodeMeta> episodeInfo;
};

OpenOutcome open(const std::string& magnet, int episode, int anilistId);

// resumeFrom: -1 keeps the stored position, 0 starts over, anything else is
// a number of seconds.
void play(int index, double resumeFrom);
void requestStop();

struct Status {
    bool done = true;
    bool playing = false;
    bool videoActive = false;
    int progress = 0;
    std::string message;

    // The swarm, for the waiting screen. Reading these from the handle beats
    // scraping them back out of the message text.
    int peers = 0;
    int seeds = 0;
    long long downloadRate = 0;  // bytes per second
    long long downloaded = 0;    // bytes this session

    // Whether Discord accepted the connection, so Settings can say so.
    bool discordConnected = false;
};
Status status();

Settings settings();
void applySettings(const Settings&);

// Starts AniList linking. In the app this opens the window we own; returns
// false with `error` set when there is no client id to use.
bool startAniListLogin(std::string& error);
void logoutAniList();

void shutdown();

}  // namespace tsuzuki::ui
