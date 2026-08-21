#include "ui.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "anilist.hpp"
#include "scan.hpp"
#include "sources/source.hpp"
#include "ui_page.hpp"

namespace tsuzuki::ui {
namespace {

using nlohmann::json;

// One torrent session for the life of the UI, plus whatever torrent is
// currently open. Guarded because httplib serves requests on many threads.
struct Engine {
    lt::session session;
    std::mutex mutex;

    std::string savePath;
    std::string openMagnet;
    lt::torrent_handle handle;
    std::shared_ptr<const lt::torrent_info> info;
    std::vector<ScannedFile> files;

    std::atomic<bool> playing{false};
    std::atomic<bool> done{true};
    std::atomic<int> progress{0};
    std::mutex messageMutex;
    std::string message;

    void setMessage(const std::string& m) {
        std::lock_guard<std::mutex> lock(messageMutex);
        message = m;
    }
    std::string getMessage() {
        std::lock_guard<std::mutex> lock(messageMutex);
        return message;
    }
};

Engine& engine() {
    static Engine e;
    return e;
}

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

const char* accuracyName(sources::Accuracy a) {
    switch (a) {
        case sources::Accuracy::High: return "high";
        case sources::Accuracy::Medium: return "medium";
        default: return "low";
    }
}

// Opens the torrent and scans it. Returns false with `err` set on failure.
bool openTorrent(Engine& e, const std::string& magnet, std::string& err) {
    if (e.openMagnet == magnet && e.info) return true;  // already open

    lt::add_torrent_params atp;
    try {
        atp = lt::parse_magnet_uri(magnet);
    } catch (const std::exception& ex) {
        err = std::string("Could not parse that magnet: ") + ex.what();
        return false;
    }
    atp.save_path = e.savePath;
    atp.flags |= lt::torrent_flags::upload_mode;

    if (e.handle.is_valid()) e.session.remove_torrent(e.handle);
    e.handle = e.session.add_torrent(std::move(atp));

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!e.handle.status().has_metadata) {
        if (std::chrono::steady_clock::now() > deadline) {
            err = "Timed out fetching torrent metadata (60s). It may have no seeders.";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    e.info = e.handle.torrent_file();
    if (!e.info) {
        err = "No torrent info.";
        return false;
    }

    const lt::file_storage& fs = e.info->layout();
    std::vector<std::pair<std::string, std::int64_t>> raw;
    for (int i = 0; i < fs.num_files(); ++i) {
        const lt::file_index_t fi{i};
        raw.emplace_back(fs.file_path(fi), fs.file_size(fi));
    }
    e.files = scanFiles(raw);
    e.openMagnet = magnet;

    if (e.files.empty()) {
        err = "No video files in this torrent.";
        return false;
    }
    return true;
}

// Buffers head+tail then hands the file to mpv. Runs on its own thread so the
// UI stays responsive.
void playFile(Engine& e, int index) {
    e.playing = true;
    e.done = false;
    e.progress = 0;
    e.setMessage("Preparing...");

    const ScannedFile* chosen = nullptr;
    for (const auto& f : e.files) {
        if (f.index == index) chosen = &f;
    }
    if (!chosen || !e.info) {
        e.setMessage("That file is no longer available.");
        e.done = true;
        e.playing = false;
        return;
    }

    const lt::file_storage& fs = e.info->layout();
    for (int i = 0; i < fs.num_files(); ++i) {
        e.handle.file_priority(lt::file_index_t{i}, lt::dont_download);
    }
    e.handle.file_priority(lt::file_index_t{chosen->index}, lt::top_priority);
    e.handle.unset_flags(lt::torrent_flags::upload_mode);
    e.handle.set_flags(lt::torrent_flags::sequential_download);

    // See main.cpp: an MP4's moov atom lives at the end, so the tail must be
    // present before any player can parse the container.
    constexpr std::int64_t kBufferBytes = 24 * 1024 * 1024;
    constexpr int kTailPieces = 4;

    const lt::file_index_t fidx{chosen->index};
    const int pieceLen = e.info->piece_length();
    const int firstPiece = static_cast<int>(e.info->map_file(fidx, 0, 0).piece);
    const int lastPiece = static_cast<int>(
        e.info->map_file(fidx, std::max<std::int64_t>(chosen->size - 1, 0), 0).piece);
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
        e.handle.piece_priority(lt::piece_index_t{p}, lt::top_priority);
        e.handle.set_piece_deadline(lt::piece_index_t{p}, 0);
    }

    for (;;) {
        int have = 0;
        for (const int p : needed) {
            if (e.handle.have_piece(lt::piece_index_t{p})) ++have;
        }
        const int pct = needed.empty() ? 100 : (have * 100 / static_cast<int>(needed.size()));
        e.progress = pct;
        e.setMessage("Buffering " + std::to_string(pct) + "% (" +
                     std::to_string(e.handle.status().num_peers) + " peers)");
        if (have == static_cast<int>(needed.size())) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    e.progress = 100;
    e.setMessage("Playing in mpv - close mpv to finish.");

    const std::string path = e.savePath + "/" + chosen->path;
    const std::string cmd = "\"\"" + findMpv() + "\" \"" + path + "\"\"";
    std::system(cmd.c_str());

    e.setMessage("Finished. Cleaning up...");
    if (e.handle.is_valid()) {
        e.session.remove_torrent(e.handle, lt::session::delete_files);
        e.handle = lt::torrent_handle();
    }
    e.info.reset();
    e.openMagnet.clear();
    e.setMessage("Done - files removed.");
    e.done = true;
    e.playing = false;
}

void openBrowser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::system(("xdg-open " + url).c_str());
#endif
}

}  // namespace

int run(int port, const std::string& savePath) {
    Engine& e = engine();
    e.savePath = savePath;

    httplib::Server server;

    server.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(kIndexHtml, "text/html; charset=utf-8");
    });

    server.Get("/api/search", [](const httplib::Request& req, httplib::Response& res) {
        json reply;
        const std::string q = req.has_param("q") ? req.get_param_value("q") : "";
        if (q.empty()) {
            reply["error"] = "Type something to search for.";
            res.set_content(reply.dump(), "application/json");
            return;
        }

        sources::Query query;
        query.title = q;
        if (req.has_param("res") && !req.get_param_value("res").empty()) {
            query.resolution = std::atoi(req.get_param_value("res").c_str());
        }

        const auto matches = anilist::search(q);
        if (!matches.empty()) {
            const auto& m = matches.front();
            query.title = m.preferred.empty() ? q : m.preferred;
            query.anilistId = m.id;
            reply["anilist"] = {{"title", query.title}, {"episodes", m.episodes}};
        }

        const std::vector<std::shared_ptr<sources::Source>> all = {
            sources::makeSeaDex(), sources::makeAnimeTosho(), sources::makeNyaa(),
            sources::makeSubsPlease(),
        };
        const auto results = sources::searchAll(all, query);

        reply["results"] = json::array();
        int n = 0;
        for (const auto& r : results) {
            reply["results"].push_back({
                {"title", r.title},
                {"magnet", r.magnet},
                {"seeders", r.seeders},
                {"size", r.size},
                {"accuracy", accuracyName(r.accuracy)},
                {"best", r.curatedBest},
                {"source", r.sourceId},
            });
            if (++n >= 40) break;
        }
        res.set_content(reply.dump(), "application/json");
    });

    server.Post("/api/open", [&e](const httplib::Request& req, httplib::Response& res) {
        json reply;
        json in;
        try {
            in = json::parse(req.body);
        } catch (const std::exception&) {
            reply["error"] = "Bad request.";
            res.set_content(reply.dump(), "application/json");
            return;
        }

        std::lock_guard<std::mutex> lock(e.mutex);
        std::string err;
        if (!openTorrent(e, in.value("magnet", ""), err)) {
            reply["error"] = err;
            res.set_content(reply.dump(), "application/json");
            return;
        }

        reply["name"] = e.info->name();
        reply["files"] = json::array();
        for (const auto& f : e.files) {
            std::string ep = "??";
            if (f.excluded) {
                ep = "--";
            } else if (f.episode.isRange()) {
                ep = std::to_string(f.episode.from) + "-" + std::to_string(f.episode.to);
            } else if (f.episode.valid) {
                ep = "EP " + std::to_string(f.episode.from);
            }
            reply["files"].push_back({
                {"index", f.index},
                {"name", f.name},
                {"size", f.size},
                {"episode", ep},
                {"skipped", f.excluded},
                {"reason", f.excludeReason},
            });
        }

        // Same rule as the CLI: a missing or ambiguous episode is reported,
        // never silently swapped for a different file.
        reply["target"] = -1;
        reply["refused"] = false;
        if (in.contains("episode") && in["episode"].is_number_integer()) {
            const int want = in["episode"].get<int>();
            reply["wanted"] = want;
            if (const ScannedFile* hit = selectEpisode(e.files, want)) {
                reply["target"] = hit->index;
            } else {
                reply["refused"] = true;
            }
        }
        res.set_content(reply.dump(), "application/json");
    });

    server.Post("/api/play", [&e](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const std::exception&) {
            res.set_content("{\"error\":\"Bad request.\"}", "application/json");
            return;
        }
        if (!e.playing) {
            const int index = in.value("index", -1);
            std::thread(
                [&e, index] {
                    std::lock_guard<std::mutex> lock(e.mutex);
                    playFile(e, index);
                })
                .detach();
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    server.Get("/api/status", [&e](const httplib::Request&, httplib::Response& res) {
        json reply{
            {"message", e.getMessage()},
            {"progress", e.progress.load()},
            {"done", e.done.load()},
        };
        res.set_content(reply.dump(), "application/json");
    });

    const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
    std::cout << "\n  Tsuzuki UI running at " << url << "\n"
              << "  Downloads: " << savePath << "\n"
              << "  Close this window to stop.\n\n";

    std::thread([url] {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        openBrowser(url);
    }).detach();

    if (!server.listen("127.0.0.1", port)) {
        std::cerr << "Could not bind port " << port
                  << ". Is another copy already running?\n";
        return 1;
    }
    return 0;
}

}  // namespace tsuzuki::ui
