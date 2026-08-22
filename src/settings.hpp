#pragma once

// Everything the user can change, in one place.
//
// Lives in its own header because two very different callers need it: the
// engine, which acts on it, and the settings screen, which draws it.

#include <string>

namespace tsuzuki {

struct Settings {
    std::string savePath;
    double speedLimit = 0;      // Mb/s, 0 = unlimited
    int maxConnections = 200;
    std::string quality = "1080";
    std::string audioLang;
    std::string subLang;
    double bufferSeconds = 0;   // 0 = size it from measured throughput
    bool deleteAfter = true;
    bool subsOn = true;

    // Interface
    std::string theme = "kuro";  // black and white, to match the mascot
    bool mascot = false;      // the companion window, pinned beside the app
    std::string titleLanguage = "romaji";   // romaji | english | native

    // Lookup
    std::string lookupPreference = "quality";  // quality | size | availability
    bool autoSelect = false;

    // Torrent client
    bool streamedDownload = false;  // only fetch what playback needs
    int torrentPort = 0;            // 0 = pick one
    int dhtPort = 0;
    bool disableDHT = false;
    bool disablePeX = false;

    // Accounts
    std::string anilistClientId;
    std::string anilistClientSecret;
    // Must match what the AniList client is registered with. Defaults to our
    // own loopback, but can be anything - including a page we do not control,
    // in which case the code is handed back manually.
    std::string anilistRedirect = "http://127.0.0.1:7654/auth/anilist";
    bool syncProgress = true;

    // Client ids for the other trackers. Registered by whoever runs the app -
    // MyAnimeList at myanimelist.net/apiconfig, Simkl in its developer
    // settings. Kitsu needs none: its client credentials are public and shared
    // by every third-party app.
    std::string malClientId;
    std::string simklClientId;

    // Interface extras
    double uiScale = 1.0;
    bool hideSpoilers = false;
    bool showAdult = false;

    // Privacy / presence
    std::string dohUrl;
    std::string discordClientId;
    bool discordPresence = true;
};

}  // namespace tsuzuki
