#pragma once

#include <string>

namespace tsuzuki::discord {

// Discord Rich Presence over the local IPC pipe.
//
// Same shape as the mpv control in player.cpp: Discord listens on a named pipe
// and speaks a small framed JSON protocol, so this needs no SDK and no extra
// dependency - just the pipe and a client id.
//
// The client id names the application Discord shows ("Playing <name>"), so it
// has to be one the user registered at discord.com/developers. It is a public
// identifier, not a secret, but it is still theirs rather than ours.

// Connects if Discord is running. Safe to call repeatedly.
bool connect(const std::string& clientId);

// Shows "Watching <show>" with the episode as the detail line. Empty show
// clears the presence.
void setWatching(const std::string& show, const std::string& episode, bool paused);

void clear();
void disconnect();
bool connected();

}  // namespace tsuzuki::discord
