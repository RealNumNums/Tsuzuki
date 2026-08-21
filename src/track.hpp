#pragma once

#include <string>

namespace tsuzuki::track {

// AniList account linking and progress sync.
//
// Desktop apps have nowhere to receive an OAuth redirect, which is normally
// the awkward part. Tsuzuki already runs a loopback server for its own UI, so
// the implicit grant can redirect straight back to it: AniList returns the
// token in the URL fragment, the callback page hands it to the engine, and no
// client secret is ever involved.
//
// The client id belongs to whoever runs the app - registered once at
// anilist.co/settings/developer - rather than being baked into the binary.

struct Account {
    bool linked = false;
    int id = 0;
    std::string name;
    std::string avatar;
};

// The client id Tsuzuki ships with. A client id is a public identifier, not a
// secret - OAuth treats desktop apps as public clients precisely because they
// cannot keep one - so baking ours in is correct, and means linking is a
// single click rather than a setup chore for every user.
//
// Empty until a Tsuzuki client is registered at anilist.co/settings/developer
// with http://127.0.0.1:7654/auth/anilist as its redirect URL.
std::string defaultClientId();

// Settings override the built-in id; otherwise the built-in one is used.
std::string resolveClientId(const std::string& fromSettings);

// URL to open in a browser to start linking. Empty if no client id is available.
std::string authorizeUrl(const std::string& clientId, int port);

void setToken(const std::string& token);
std::string token();
bool linked();
void logout();

// Cached where possible; pass force to re-query AniList.
Account account(bool force = false);

// Sets progress on the user's list, moving the entry to CURRENT. Returns false
// if not linked, or if AniList rejected it.
bool updateProgress(int mediaId, int progress);

void load();  // read the stored token at startup

}  // namespace tsuzuki::track
