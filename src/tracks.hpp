#pragma once

// Picking the audio and subtitle track someone actually meant.
//
// mpv's --alang/--slang match on the language tag alone, which is not enough
// for anime releases. A dual-audio episode routinely carries two English
// subtitle tracks: "Signs & Songs", which translates on-screen text only and
// is meant to sit under an English dub, and the full dialogue track. Matching
// on "eng" picks whichever comes first, so asking for English subtitles gets
// you a nearly blank screen about half the time.
//
// So the tracks are read back from mpv once it has loaded the file and chosen
// here instead, on the language tag and on what the track calls itself.

#include "player.hpp"

#include <string>
#include <vector>

namespace tsuzuki::tracks {

struct Choice {
    int audioId = -1;  // -1 means leave mpv's own choice alone
    int subId = -1;    // 0 means subtitles off
    std::string why;   // one line for the log/UI, e.g. "English dub + signs"
};

// `audioLang` and `subLang` are the settings values ("", "eng", "jpn").
// An empty preference means "no opinion", and mpv's choice is left as it is.
Choice choose(const std::vector<player::Track>& tracks, const std::string& audioLang,
              const std::string& subLang, bool subsOn);

// True when two language tags mean the same language - "en", "eng" and
// "english" all being the same thing, which is the sort of thing release
// groups are not consistent about.
bool sameLanguage(const std::string& a, const std::string& b);

}  // namespace tsuzuki::tracks
