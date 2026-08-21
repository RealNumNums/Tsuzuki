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

// AniList rejects the implicit grant (unsupported_grant_type) - it is
// deprecated in OAuth 2.1 and they have turned it off - so this uses the
// authorization code flow. That needs a client secret, which is why the
// secret exists here at all despite the app being a public client.
//
// The built-in credentials live in secrets.local.hpp, which is gitignored:
// the binary carries them so linking is one click, while the public repo
// carries none. Anyone building from source supplies their own, or enters
// them in Settings.
std::string defaultClientId();
std::string defaultClientSecret();

std::string resolveClientId(const std::string& fromSettings);
std::string resolveClientSecret(const std::string& fromSettings);

// URL to open in a browser to start linking. Empty if no client id is available.
std::string authorizeUrl(const std::string& clientId, const std::string& redirect);

// Implicit grant, deliberately without redirect_uri. AniList then redirects to
// whatever the client is registered with, and the host window intercepts that
// navigation to read the token out of the fragment - so the registered URL
// never has to be reachable, or even correct.
std::string implicitAuthorizeUrl(const std::string& clientId);

// Swaps the ?code= AniList sends back for an access token. Returns false with
// `error` set on failure.
bool exchangeCode(const std::string& code, const std::string& clientId,
                  const std::string& clientSecret, const std::string& redirect,
                  std::string& error);

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
