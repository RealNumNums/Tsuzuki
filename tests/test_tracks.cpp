// Exercises the track chooser against the track layouts real releases ship.
#include "tracks.hpp"

#include <cstdio>
#include <string>
#include <vector>

using tsuzuki::player::Track;
using tsuzuki::tracks::choose;

static int failures = 0;

static Track T(int id, const char* type, const char* lang, const char* title,
               bool selected = false, bool def = false) {
    Track t;
    t.id = id;
    t.type = type;
    t.lang = lang;
    t.title = title;
    t.selected = selected;
    t.isDefault = def;
    return t;
}

static void check(const char* name, int got, int want) {
    const bool ok = got == want;
    if (!ok) ++failures;
    std::printf("%-58s %-8s got=%d want=%d\n", name, ok ? "ok" : "FAIL", got, want);
}

int main() {
    // The case that motivated all of this: dual audio, two english sub tracks.
    {
        const std::vector<Track> t = {
            T(1, "audio", "jpn", "Japanese", true, true),
            T(2, "audio", "eng", "English"),
            T(3, "sub", "eng", "Signs & Songs", true, true),
            T(4, "sub", "eng", "Full Subtitles"),
        };
        auto c = choose(t, "jpn", "eng", true);
        check("japanese audio -> full subtitles, not signs", c.subId, 4);
        check("japanese audio -> japanese track", c.audioId, 1);

        auto d = choose(t, "eng", "eng", true);
        check("english dub -> english audio", d.audioId, 2);
        check("english dub -> signs track is the right one", d.subId, 3);
    }

    // Only one english sub track: nothing clever to do, just take it.
    {
        const std::vector<Track> t = {
            T(1, "audio", "jpn", "Japanese", true, true),
            T(2, "sub", "eng", "English"),
        };
        auto c = choose(t, "jpn", "eng", true);
        check("single english sub is chosen", c.subId, 2);
    }

    // Language in the title, tag missing - common with older rips.
    {
        const std::vector<Track> t = {
            T(1, "audio", "", "Japanese", true, true),
            T(2, "audio", "", "English"),
            T(3, "sub", "eng", "Full"),
        };
        auto c = choose(t, "eng", "eng", true);
        check("title-only language is too weak to force audio", c.audioId, -1);
    }

    // Commentary must never win.
    {
        const std::vector<Track> t = {
            T(1, "audio", "eng", "Commentary", true, true),
            T(2, "audio", "eng", "English"),
            T(3, "sub", "eng", "Full Subtitles"),
        };
        auto c = choose(t, "eng", "eng", true);
        check("commentary is never picked", c.audioId, 2);
    }

    // Subtitles switched off wins over everything.
    {
        const std::vector<Track> t = {
            T(1, "audio", "jpn", "Japanese", true, true),
            T(2, "sub", "eng", "Full Subtitles"),
        };
        auto c = choose(t, "jpn", "eng", false);
        check("subtitles off means off", c.subId, 0);
    }

    // No preference: leave mpv alone rather than second-guessing it.
    {
        const std::vector<Track> t = {
            T(1, "audio", "jpn", "Japanese", true, true),
            T(2, "audio", "eng", "English"),
            T(3, "sub", "eng", "Full Subtitles"),
        };
        auto c = choose(t, "", "", true);
        check("no preference leaves audio alone", c.audioId, -1);
        check("no preference leaves subtitles alone", c.subId, -1);
    }

    // A language nobody has: do not fall back to the wrong one.
    {
        const std::vector<Track> t = {
            T(1, "audio", "jpn", "Japanese", true, true),
            T(2, "sub", "eng", "Full Subtitles"),
        };
        auto c = choose(t, "jpn", "spa", true);
        check("missing subtitle language picks nothing", c.subId, -1);
    }

    // Tag spelling should not matter.
    {
        const std::vector<Track> t = {
            T(1, "audio", "ja", "", true, true),
            T(2, "audio", "en", ""),
            T(3, "sub", "en", "Full Subtitles"),
            T(4, "sub", "en", "Signs"),
        };
        auto c = choose(t, "jpn", "eng", true);
        check("iso 639-1 tags match 639-2 preferences", c.audioId, 1);
        check("full subtitles still win with short tags", c.subId, 3);
    }

    std::printf("\n%s\n", failures == 0 ? "all track choices correct"
                                        : "SOME CHOICES WRONG");
    return failures == 0 ? 0 : 1;
}
