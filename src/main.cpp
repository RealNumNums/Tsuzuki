// Tsuzuki
//
//   tsuzuki <magnet|torrent> [--episode N] [--save-path DIR] [--keep]
//   tsuzuki search "<title>" [--episode N] [--res 1080] [--save-path DIR] [--keep]
//
// Design rule, and the reason this exists: it never plays a file you did not
// ask for. If the requested episode is missing or ambiguous, it says so and
// shows the table.

#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#include "anilist.hpp"
#include "ui.hpp"
#include "scan.hpp"
#include "sources/source.hpp"

namespace {

// True when we own the console window - i.e. the exe was double-clicked from
// Explorer rather than run from an existing terminal. Windows destroys that
// console the moment the process exits, which is why double-clicking a CLI
// tool looks like "nothing happened".
bool ownsConsole() {
#ifdef _WIN32
    DWORD pids[4] = {};
    const DWORD n = GetConsoleProcessList(pids, 4);
    return n <= 1;
#else
    return false;
#endif
}

// Keeps that console open long enough to read, on every exit path.
struct PauseOnExit {
    bool active = false;
    ~PauseOnExit() {
        if (!active) return;
        std::cout << "\nPress Enter to close..." << std::flush;
        std::cin.clear();
        std::string discard;
        std::getline(std::cin, discard);
    }
};

struct Args {
    std::string mode = "play";  // "play" | "search"
    std::string uri;            // magnet / torrent, or search text
    std::optional<int> episode;
    std::optional<int> resolution;
    std::string savePath = "downloads";
    bool keep = false;
};

std::optional<Args> parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        // "search" is the first non-flag argument, wherever it lands - the
        // launcher script prepends --save-path, so it is not always argv[1].
        if (arg == "search" && a.mode == "play" && a.uri.empty()) {
            a.mode = "search";
        } else if (arg == "ui" && a.mode == "play" && a.uri.empty()) {
            a.mode = "ui";
            a.uri = "ui";
        } else if (arg == "--episode" && i + 1 < argc) {
            a.episode = std::atoi(argv[++i]);
        } else if (arg == "--res" && i + 1 < argc) {
            a.resolution = std::atoi(argv[++i]);
        } else if (arg == "--save-path" && i + 1 < argc) {
            a.savePath = argv[++i];
        } else if (arg == "--keep") {
            a.keep = true;
        } else if (!arg.empty() && arg[0] != '-') {
            if (a.uri.empty()) a.uri = arg;
        }
    }
    if (a.uri.empty()) return std::nullopt;
    return a;
}

void usage() {
    std::cout
        << "usage:\n"
           "  tsuzuki <magnet|torrent> [--episode N] [--save-path DIR] [--keep]\n"
           "  tsuzuki search \"<title>\" [--episode N] [--res 1080] "
           "[--save-path DIR] [--keep]\n";
}

// Installers rarely put mpv on PATH (winget's shinchiro.mpv drops it in
// "Program Files\MPV Player"), so look in the usual places before falling
// back to a bare PATH lookup. Override with TSUZUKI_MPV.
std::string findMpv() {
    if (const char* env = std::getenv("TSUZUKI_MPV")) {
        if (*env) return env;
    }
    static const char* candidates[] = {
        "C:/Program Files/MPV Player/mpv.exe",
        "C:/Program Files/mpv/mpv.exe",
        "C:/Program Files (x86)/MPV Player/mpv.exe",
        "C:/Program Files (x86)/mpv/mpv.exe",
    };
    for (const char* c : candidates) {
        std::error_code ec;
        if (std::filesystem::exists(c, ec)) return c;
    }
    return "mpv";
}

std::string humanSize(std::int64_t bytes) {
    static const char* units[] = {"B", "K", "M", "G", "T"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), v < 10.0 && u > 0 ? "%.1f%s" : "%.0f%s", v, units[u]);
    return buf;
}

const char* accuracyLabel(tsuzuki::sources::Accuracy a) {
    using tsuzuki::sources::Accuracy;
    switch (a) {
        case Accuracy::High: return "high";
        case Accuracy::Medium: return "med";
        default: return "low";
    }
}

/* ------------------------------------------------------------ M2: search */

// True when a title plausibly IS what the user typed, ignoring case,
// punctuation and spacing - so "spy x family" matches "Spy x Family" but not
// "Spy Kyoushitsu".
std::string squash(const std::string& s) {
    std::string out;
    for (const unsigned char c : s) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

bool looksLikeMatch(const std::string& typed, const tsuzuki::anilist::Media& m) {
    const std::string want = squash(typed);
    if (want.empty()) return false;
    for (const std::string& candidate :
         {m.romaji, m.english, m.native, m.preferred}) {
        const std::string have = squash(candidate);
        if (have.empty()) continue;
        if (have == want || have.find(want) != std::string::npos) return true;
    }
    for (const std::string& syn : m.synonyms) {
        const std::string have = squash(syn);
        if (!have.empty() && have.find(want) != std::string::npos) return true;
    }
    return false;
}

// Returns a chosen magnet, or empty when the user backs out.
std::string runSearch(const Args& args) {
    namespace src = tsuzuki::sources;

    src::Query q;
    q.title = args.uri;
    q.episode = args.episode;
    q.resolution = args.resolution;

    // AniList gives us the canonical title (better search hits) and the ID
    // SeaDex is keyed by.
    const auto matches = tsuzuki::anilist::search(args.uri);
    if (!matches.empty()) {
        std::size_t pick = 0;

        // AniList's top hit is not always right - "spy x family" returns
        // "Spy Kyoushitsu" first. Accepting it silently would repeat, one
        // layer up, exactly the mistake this tool exists to avoid. So confirm
        // whenever the top hit does not obviously match what was typed.
        if (matches.size() > 1 && !looksLikeMatch(args.uri, matches.front())) {
            std::cout << "\nanilist: \"" << args.uri
                      << "\" is ambiguous - which did you mean?\n\n";
            for (std::size_t i = 0; i < matches.size(); ++i) {
                const auto& m = matches[i];
                std::printf("  %zu  %-46s %s%s\n", i, m.preferred.c_str(),
                            m.year ? (std::to_string(m.year) + " ").c_str() : "",
                            m.episodes ? (std::to_string(m.episodes) + " eps").c_str()
                                       : "ongoing");
            }
            std::cout << "\npick a # [0]: " << std::flush;
            std::string line;
            std::getline(std::cin >> std::ws, line);
            if (!line.empty()) {
                const int idx = std::atoi(line.c_str());
                if (idx >= 0 && idx < static_cast<int>(matches.size())) {
                    pick = static_cast<std::size_t>(idx);
                }
            }
            std::cout << "\n";
        }

        const auto& m = matches[pick];
        q.title = m.preferred.empty() ? args.uri : m.preferred;
        q.anilistId = m.id;
        for (const auto& s : m.synonyms) q.altTitles.push_back(s);
        std::cout << "anilist: " << q.title << " (#" << m.id << ", "
                  << (m.episodes ? std::to_string(m.episodes) + " eps" : "ongoing")
                  << ")\n";
    } else {
        std::cout << "anilist: no match, searching raw title\n";
    }

    std::cout << "searching sources...\n";
    const std::vector<std::shared_ptr<src::Source>> sources = {
        src::makeSeaDex(), src::makeAnimeTosho(), src::makeNyaa(), src::makeSubsPlease(),
    };
    const auto results = src::searchAll(sources, q);

    if (results.empty()) {
        std::cerr << "no results.\n";
        return {};
    }

    std::printf("\n  %-3s %-5s %-6s %-52s %8s %6s\n", "#", "ACC", "SEED", "TITLE", "SIZE",
                "SOURCE");
    std::printf("  %s\n", std::string(88, '-').c_str());
    int i = 0;
    for (const auto& r : results) {
        std::string label = r.title;
        if (label.size() > 52) label = label.substr(0, 49) + "...";
        const std::string seed = r.seeders ? std::to_string(r.seeders) : "-";
        std::printf("  %-3d %-5s %-6s %-52s %8s %6s\n", i,
                    r.curatedBest ? "BEST" : accuracyLabel(r.accuracy), seed.c_str(),
                    label.c_str(), humanSize(r.size).c_str(), r.sourceId.c_str());
        ++i;
        if (i >= 25) break;
    }
    std::printf("\n");

    std::cout << "pick a # to open (or blank to quit): " << std::flush;
    std::string line;
    std::getline(std::cin >> std::ws, line);
    if (line.empty()) return {};

    const int pickIdx = std::atoi(line.c_str());
    if (pickIdx < 0 || pickIdx >= static_cast<int>(results.size())) {
        std::cerr << "no such result\n";
        return {};
    }
    return results[pickIdx].magnet;
}

/* --------------------------------------------------- M4: verified cleanup */

// Hayase leaks torrent data when a delete races the player's file handles, and
// never checks. So: ask libtorrent to delete, wait for the alert that says
// whether it worked, and confirm on disk before claiming success.
void cleanUp(lt::session& session, lt::torrent_handle& handle, const std::string& path) {
    if (!handle.is_valid()) return;

    session.remove_torrent(handle, lt::session::delete_files);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool resolved = false;
    bool deleted = false;
    std::string failure;

    while (!resolved && std::chrono::steady_clock::now() < deadline) {
        std::vector<lt::alert*> alerts;
        session.pop_alerts(&alerts);
        for (const lt::alert* a : alerts) {
            if (lt::alert_cast<lt::torrent_deleted_alert>(a)) {
                deleted = true;
                resolved = true;
            } else if (const auto* f = lt::alert_cast<lt::torrent_delete_failed_alert>(a)) {
                failure = f->error.message();
                resolved = true;
            }
        }
        if (!resolved) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::error_code ec;
    const bool stillThere = std::filesystem::exists(path, ec);

    if (deleted && !stillThere) {
        std::cout << "cleaned up.\n";
    } else if (stillThere) {
        // Report the actual state rather than assuming the request worked.
        std::cerr << "cleanup incomplete: " << path << " still exists"
                  << (failure.empty() ? "" : " (" + failure + ")")
                  << "\ndelete it manually, or re-run with --keep if that was intended.\n";
    } else if (!resolved) {
        std::cerr << "cleanup unconfirmed: no result within 20s.\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    PauseOnExit keepOpen{ownsConsole()};

    std::optional<Args> args;
    if (argc <= 1 && keepOpen.active) {
        // Double-clicked from Explorer: that means "just open the thing", so
        // start the UI rather than interrogating them in a console window.
        Args a;
        a.mode = "ui";
        a.uri = "ui";
        args = a;
    } else {
        args = parseArgs(argc, argv);
    }

    if (!args) {
        usage();
        return 1;
    }

    if (args->mode == "ui") {
        keepOpen.active = false;  // the server prints its own URL and blocks
        std::string path = args->savePath;
        if (path == "downloads") {
            if (const char* tmp = std::getenv("TEMP")) path = std::string(tmp) + "\\tsuzuki";
        }
        return tsuzuki::ui::run(7654, path);
    }

    if (args->mode == "search") {
        const std::string magnet = runSearch(*args);
        if (magnet.empty()) return 1;
        args->uri = magnet;
    }

    lt::session session;

    lt::add_torrent_params atp;
    try {
        atp = lt::parse_magnet_uri(args->uri);
    } catch (const std::exception& e) {
        std::cerr << "could not parse magnet: " << e.what() << "\n";
        return 1;
    }
    atp.save_path = args->savePath;
    // Don't pull any payload until we know what the files are.
    atp.flags |= lt::torrent_flags::upload_mode;

    lt::torrent_handle handle = session.add_torrent(std::move(atp));

    std::cout << "fetching metadata...\n";
    while (!handle.status().has_metadata) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    const std::shared_ptr<const lt::torrent_info> info = handle.torrent_file();
    if (!info) {
        std::cerr << "no torrent info\n";
        return 1;
    }

    // libtorrent 2.1 renamed files() -> layout(); files() now only exists in
    // the deprecated ABI block, which vcpkg's build compiles out.
    const lt::file_storage& fs = info->layout();
    std::vector<std::pair<std::string, std::int64_t>> files;
    files.reserve(static_cast<std::size_t>(fs.num_files()));
    for (int i = 0; i < fs.num_files(); ++i) {
        const lt::file_index_t fi{i};
        files.emplace_back(fs.file_path(fi), fs.file_size(fi));
    }

    const std::vector<tsuzuki::ScannedFile> scanned = tsuzuki::scanFiles(files);
    if (scanned.empty()) {
        std::cerr << "no video files in this torrent\n";
        return 1;
    }

    std::cout << "\n" << info->name() << "\n";
    tsuzuki::printTable(scanned);

    const tsuzuki::ScannedFile* chosen = nullptr;

    if (args->episode) {
        chosen = tsuzuki::selectEpisode(scanned, *args->episode);
        if (!chosen) {
            // Deliberately a hard stop. Hayase would silently play episode 1
            // or the first file here; that is the bug this tool exists to fix.
            std::cerr << "episode " << *args->episode
                      << " is not in this torrent, or more than one file claims it.\n"
                         "nothing was played. pick a row number above with --episode "
                         "omitted, or use a different release.\n";
            return 2;
        }
        std::cout << "episode " << *args->episode << " -> " << chosen->name << "\n";
    } else {
        std::cout << "pick a # to play: " << std::flush;
        int want = -1;
        if (!(std::cin >> want)) return 1;
        for (const auto& f : scanned) {
            if (f.index == want) chosen = &f;
        }
        if (!chosen) {
            std::cerr << "no file with index " << want << "\n";
            return 1;
        }
    }

    // Download only the chosen file, front to back.
    for (int i = 0; i < fs.num_files(); ++i) {
        handle.file_priority(lt::file_index_t{i}, lt::dont_download);
    }
    handle.file_priority(lt::file_index_t{chosen->index}, lt::top_priority);
    handle.unset_flags(lt::torrent_flags::upload_mode);
    handle.set_flags(lt::torrent_flags::sequential_download);

    // An MP4's moov atom (the index) usually sits at the END of the file, so a
    // player cannot parse the container until the tail arrives - no amount of
    // head buffering helps. Streaming clients therefore fetch both ends first.
    // Waiting on bytes-downloaded is also wrong: file_progress counts bytes
    // anywhere in the file, not a contiguous run from the start.
    constexpr std::int64_t kBufferBytes = 24 * 1024 * 1024;
    constexpr int kTailPieces = 4;

    const lt::file_index_t fidx{chosen->index};
    const int pieceLen = info->piece_length();
    const int firstPiece = static_cast<int>(info->map_file(fidx, 0, 0).piece);
    const int lastPiece = static_cast<int>(
        info->map_file(fidx, std::max<std::int64_t>(chosen->size - 1, 0), 0).piece);

    const int headPieces = std::max(
        1, static_cast<int>((std::min(kBufferBytes, chosen->size) + pieceLen - 1) / pieceLen));

    std::vector<int> needed;
    for (int p = firstPiece; p <= std::min(firstPiece + headPieces - 1, lastPiece); ++p) {
        needed.push_back(p);
    }
    for (int p = std::max(lastPiece - kTailPieces + 1, firstPiece); p <= lastPiece; ++p) {
        if (std::find(needed.begin(), needed.end(), p) == needed.end()) needed.push_back(p);
    }
    for (const int p : needed) {
        handle.piece_priority(lt::piece_index_t{p}, lt::top_priority);
        handle.set_piece_deadline(lt::piece_index_t{p}, 0);
    }

    std::cout << "buffering head+tail (" << needed.size() << " pieces)";
    for (;;) {
        bool ready = true;
        for (const int p : needed) {
            if (!handle.have_piece(lt::piece_index_t{p})) {
                ready = false;
                break;
            }
        }
        if (ready) break;
        std::cout << "." << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << " ok\n";

    const std::string path = args->savePath + "/" + chosen->path;
    // cmd.exe strips the outer pair of quotes, so when the executable path
    // itself contains spaces the whole command needs wrapping a second time.
    const std::string cmd = "\"\"" + findMpv() + "\" \"" + path + "\"\"";
    std::cout << "launching: " << cmd << "\n";
    std::system(cmd.c_str());

    if (args->keep) {
        std::cout << "kept: " << path << "\n";
    } else {
        cleanUp(session, handle, args->savePath + "/" + info->name());
    }

    return 0;
}
