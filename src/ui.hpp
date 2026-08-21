#pragma once

#include <string>
#include <vector>

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

// Settings, for the screen that edits them. applySettings writes to disk and
// pushes the parts the running session cares about (speed limit, connection
// cap, DoH resolver) straight into libtorrent.
Settings settings();
void applySettings(const Settings&);

// Starts AniList linking. In the app this opens the window we own; returns
// false with `error` set when there is no client id to use.
bool startAniListLogin(std::string& error);
void logoutAniList();

void shutdown();

}  // namespace tsuzuki::ui
