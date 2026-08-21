// Tsuzuki - milestone 1
//
//   tsuzuki "magnet:?xt=urn:btih:..." [--episode N] [--save-path DIR] [--keep]
//
// Fetches torrent metadata, maps every file to an episode with Anitomy, prints
// the mapping, streams the file you pick, and hands it to mpv.
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
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "scan.hpp"

namespace {

struct Args {
    std::string uri;
    std::optional<int> episode;
    std::string savePath = "downloads";
    bool keep = false;
};

std::optional<Args> parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--episode" && i + 1 < argc) {
            a.episode = std::atoi(argv[++i]);
        } else if (arg == "--save-path" && i + 1 < argc) {
            a.savePath = argv[++i];
        } else if (arg == "--keep") {
            a.keep = true;
        } else if (!arg.empty() && arg[0] != '-') {
            a.uri = arg;
        }
    }
    if (a.uri.empty()) return std::nullopt;
    return a;
}

void usage() {
    std::cout << "usage: tsuzuki <magnet-or-torrent> [--episode N] "
                 "[--save-path DIR] [--keep]\n";
}

}  // namespace

int main(int argc, char** argv) {
    const auto args = parseArgs(argc, argv);
    if (!args) {
        usage();
        return 1;
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
    files.reserve((std::size_t)fs.num_files());
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

    constexpr std::int64_t kBufferBytes = 24 * 1024 * 1024;
    const std::int64_t target = std::min<std::int64_t>(kBufferBytes, chosen->size);

    std::cout << "buffering";
    for (;;) {
        std::vector<std::int64_t> progress;
        handle.file_progress(progress);
        const std::int64_t got =
            (std::size_t)chosen->index < progress.size() ? progress[chosen->index] : 0;
        if (got >= target) break;
        std::cout << "." << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << " ok\n";

    const std::string path = args->savePath + "/" + chosen->path;
    const std::string cmd = "mpv \"" + path + "\"";
    std::cout << "launching: " << cmd << "\n";
    std::system(cmd.c_str());

    if (!args->keep) {
        // Milestone 4 lands here: remove_torrent with delete_files, and verify
        // it actually happened rather than assuming (Hayase leaks on locks).
        std::cout << "(--keep not set; cleanup lands in M4)\n";
    }

    return 0;
}
