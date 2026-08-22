#include "tracks.hpp"

#include <algorithm>
#include <cctype>

namespace tsuzuki::tracks {
namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool has(const std::string& haystack, const char* needle) {
    return haystack.find(needle) != std::string::npos;
}

// Reduce a tag to a canonical language name. Release groups use ISO 639-1,
// 639-2 and plain English names more or less interchangeably.
std::string canonical(const std::string& tag) {
    const std::string t = lower(tag);
    if (t.empty()) return {};
    if (t == "en" || t == "eng" || has(t, "english")) return "english";
    if (t == "ja" || t == "jp" || t == "jpn" || has(t, "japanese")) return "japanese";
    if (t == "es" || t == "spa" || has(t, "spanish")) return "spanish";
    if (t == "pt" || t == "por" || has(t, "portuguese")) return "portuguese";
    if (t == "de" || t == "ger" || t == "deu" || has(t, "german")) return "german";
    if (t == "fr" || t == "fre" || t == "fra" || has(t, "french")) return "french";
    if (t == "it" || t == "ita" || has(t, "italian")) return "italian";
    if (t == "ar" || t == "ara" || has(t, "arabic")) return "arabic";
    return t;
}

// A track that only translates on-screen text and song lyrics. Correct under a
// dub, useless under Japanese audio.
bool isSignsOnly(const player::Track& t) {
    const std::string s = lower(t.title);
    if (has(s, "sign") || has(s, "song")) return true;
    if (has(s, "forced")) return true;
    return false;
}

bool isFullDialogue(const player::Track& t) {
    const std::string s = lower(t.title);
    return has(s, "full") || has(s, "dialog") || has(s, "dialogue");
}

bool isCommentary(const player::Track& t) {
    const std::string s = lower(t.title);
    return has(s, "commentary");
}

}  // namespace

bool sameLanguage(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    return canonical(a) == canonical(b);
}

Choice choose(const std::vector<player::Track>& all, const std::string& audioLang,
              const std::string& subLang, bool subsOn) {
    Choice out;

    std::vector<const player::Track*> audio, subs;
    for (const auto& t : all) {
        if (t.type == "audio") audio.push_back(&t);
        else if (t.type == "sub") subs.push_back(&t);
    }

    // ---- audio ---------------------------------------------------------
    const player::Track* pickedAudio = nullptr;
    if (!audioLang.empty() && audio.size() > 1) {
        int best = 0;
        for (const auto* t : audio) {
            int score = 0;
            if (sameLanguage(t->lang, audioLang)) score += 100;
            // A title can carry the language when the tag does not.
            if (canonical(t->title) == canonical(audioLang)) score += 40;
            if (isCommentary(*t)) score -= 200;  // never what anyone means
            if (t->isDefault) score += 5;
            if (score > best) {
                best = score;
                pickedAudio = t;
            }
        }
        // Only act on a real language match; a title-only hint is too weak to
        // override what mpv already decided.
        if (pickedAudio && best >= 100) out.audioId = pickedAudio->id;
    }

    // Whatever is actually going to play, for deciding about subtitles.
    const player::Track* effectiveAudio = pickedAudio;
    if (!effectiveAudio) {
        for (const auto* t : audio) {
            if (t->selected) effectiveAudio = t;
        }
    }
    const bool dubbed =
        effectiveAudio && !audioLang.empty() && sameLanguage(effectiveAudio->lang, "english") &&
        !sameLanguage(effectiveAudio->lang, "japanese");

    // ---- subtitles -------------------------------------------------------
    if (!subsOn) {
        out.subId = 0;
        out.why = "subtitles off";
        return out;
    }

    if (!subLang.empty() && !subs.empty()) {
        int best = -1000;
        const player::Track* picked = nullptr;
        for (const auto* t : subs) {
            int score = 0;
            if (sameLanguage(t->lang, subLang)) score += 100;
            if (canonical(t->title) == canonical(subLang)) score += 40;
            if (isCommentary(*t)) score -= 200;

            // The point of the whole exercise: under a dub the signs track is
            // the right one, and under Japanese audio it is close to useless.
            if (dubbed) {
                if (isSignsOnly(*t)) score += 50;
                if (isFullDialogue(*t)) score -= 10;
            } else {
                if (isSignsOnly(*t)) score -= 80;
                if (isFullDialogue(*t)) score += 50;
            }
            if (t->isDefault) score += 5;

            if (score > best) {
                best = score;
                picked = t;
            }
        }
        if (picked && best >= 100) out.subId = picked->id;

        if (picked) {
            out.why = dubbed ? "english audio, signs preferred"
                             : "original audio, full subtitles preferred";
        }
    }

    return out;
}

}  // namespace tsuzuki::tracks
