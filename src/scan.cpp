#include "scan.hpp"

#include <anitomy/anitomy.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tsuzuki {
namespace {

#ifdef _WIN32
std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}
std::string toUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}
#else
std::wstring toWide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
std::string toUtf8(const std::wstring& s) { return std::string(s.begin(), s.end()); }
#endif

constexpr std::array<const char*, 9> kVideoExt = {
    ".mkv", ".mp4", ".avi", ".mov", ".webm", ".m2ts", ".ts", ".ogm", ".wmv"};

// Anitomy tags these; they are real files but never the episode you asked for.
constexpr std::array<const char*, 8> kTypeExclusions = {
    "OP", "OPENING", "ED", "ENDING", "NCOP", "NCED", "PREVIEW", "PV"};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool isVideo(const std::string& name) {
    const std::string l = lower(name);
    for (const char* ext : kVideoExt) {
        if (l.size() > std::strlen(ext) &&
            l.compare(l.size() - std::strlen(ext), std::strlen(ext), ext) == 0) {
            return true;
        }
    }
    return false;
}

std::string baseName(const std::string& path) {
    const auto pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::optional<int> toInt(const std::wstring& w) {
    if (w.empty()) return std::nullopt;
    try {
        size_t consumed = 0;
        const int v = std::stoi(w, &consumed);
        // reject things like "06.5" (recap/special) - not a whole episode
        if (consumed != w.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

std::string humanSize(std::int64_t bytes) {
    static const char* units[] = {"B", "K", "M", "G", "T"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    std::ostringstream os;
    os << std::fixed << std::setprecision(v < 10.0 && u > 0 ? 1 : 0) << v << units[u];
    return os.str();
}

}  // namespace

Episode episodeFromName(const std::string& name) {
    Episode out;
    anitomy::Anitomy parser;
    if (!parser.Parse(toWide(name))) return out;

    const auto& el = parser.elements();
    const auto eps = el.get_all(anitomy::kElementEpisodeNumber);
    if (eps.empty()) return out;

    const auto first = toInt(eps[0]);
    if (!first) return out;

    out.from = *first;
    out.to = *first;
    out.valid = true;
    if (eps.size() > 1) {
        if (const auto second = toInt(eps[1])) {
            if (*second > *first) out.to = *second;
        }
    }
    return out;
}

std::vector<ScannedFile> scanFiles(
    const std::vector<std::pair<std::string, std::int64_t>>& files) {
    std::vector<ScannedFile> out;
    int idx = -1;

    for (const auto& [path, size] : files) {
        ++idx;
        if (!isVideo(path)) continue;

        ScannedFile f;
        f.index = idx;
        f.path = path;
        f.name = baseName(path);
        f.size = size;

        anitomy::Anitomy parser;
        parser.Parse(toWide(f.name));
        const auto& el = parser.elements();

        f.title = toUtf8(el.get(anitomy::kElementAnimeTitle));

        if (const auto season = toInt(el.get(anitomy::kElementAnimeSeason))) {
            f.season = season;
        }

        f.type = toUtf8(el.get(anitomy::kElementAnimeType));
        const std::string upperType = [&] {
            std::string t = f.type;
            std::transform(t.begin(), t.end(), t.begin(),
                           [](unsigned char c) { return (char)std::toupper(c); });
            return t;
        }();
        for (const char* ex : kTypeExclusions) {
            if (upperType == ex) {
                f.excluded = true;
                f.excludeReason = f.type;
                break;
            }
        }

        // episode_number can carry two values for a multi-episode file
        const auto eps = el.get_all(anitomy::kElementEpisodeNumber);
        if (!eps.empty()) {
            if (const auto first = toInt(eps[0])) {
                f.episode.from = *first;
                f.episode.to = *first;
                f.episode.valid = true;
                if (eps.size() > 1) {
                    if (const auto second = toInt(eps[1])) {
                        if (*second > *first) f.episode.to = *second;
                    }
                }
            }
        }

        out.push_back(std::move(f));
    }

    std::sort(out.begin(), out.end(), [](const ScannedFile& a, const ScannedFile& b) {
        const int sa = a.season.value_or(1), sb = b.season.value_or(1);
        if (sa != sb) return sa < sb;  // ascending; Hayase sorts descending here
        if (a.episode.valid != b.episode.valid) return a.episode.valid;
        return a.episode.from < b.episode.from;
    });

    return out;
}

const ScannedFile* selectEpisode(const std::vector<ScannedFile>& files, int wanted) {
    const ScannedFile* exact = nullptr;
    int exactCount = 0;
    for (const auto& f : files) {
        if (f.excluded) continue;
        if (f.episode.isExactly(wanted)) {
            exact = &f;
            ++exactCount;
        }
    }
    // More than one file claims this episode (duplicate rips, multiple seasons
    // with the same relative numbering). Ambiguous is not a match - make the
    // user choose rather than guessing for them.
    if (exactCount == 1) return exact;
    if (exactCount > 1) return nullptr;

    const ScannedFile* ranged = nullptr;
    int rangeCount = 0;
    for (const auto& f : files) {
        if (f.excluded) continue;
        if (f.episode.isRange() && f.episode.contains(wanted)) {
            ranged = &f;
            ++rangeCount;
        }
    }
    if (rangeCount == 1) return ranged;

    return nullptr;
}

void printTable(const std::vector<ScannedFile>& files) {
    std::printf("\n  %-3s %-3s %-6s %-44s %8s\n", "#", "S", "EP", "TITLE", "SIZE");
    std::printf("  %s\n", std::string(70, '-').c_str());

    for (const auto& f : files) {
        std::string ep;
        if (f.excluded) {
            ep = "--";
        } else if (f.episode.isRange()) {
            ep = std::to_string(f.episode.from) + "-" + std::to_string(f.episode.to);
        } else if (f.episode.valid) {
            ep = (f.episode.from < 10 ? "0" : "") + std::to_string(f.episode.from);
        } else {
            ep = "??";
        }

        std::string label = f.name;
        if (label.size() > 44) label = label.substr(0, 41) + "...";

        std::string season = f.season ? std::to_string(*f.season) : "-";

        std::printf("  %-3d %-3s %-6s %-44s %8s%s\n", f.index, season.c_str(), ep.c_str(),
                    label.c_str(), humanSize(f.size).c_str(),
                    f.excluded ? ("   (skipped: " + f.excludeReason + ")").c_str() : "");
    }
    std::printf("\n");
}

}  // namespace tsuzuki
