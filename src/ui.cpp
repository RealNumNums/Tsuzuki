#include "ui.hpp"

#include "library.hpp"
#include "settings.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include "anilist.hpp"
#include "http.hpp"
#include "player.hpp"
#include "scan.hpp"
#include "sources/source.hpp"
#include "discord.hpp"
#include "track.hpp"
#include "ui_page.hpp"

namespace tsuzuki::ui {
namespace {

using tsuzuki::Settings;

using nlohmann::json;

// One torrent session for the life of the UI, plus whatever torrent is
// currently open. Guarded because httplib serves requests on many threads.
static lt::settings_pack fastMetadataSettings() {
    lt::settings_pack sp;
    sp.set_bool(lt::settings_pack::enable_dht, true);
    sp.set_bool(lt::settings_pack::enable_lsd, true);
    sp.set_bool(lt::settings_pack::enable_upnp, true);
    sp.set_bool(lt::settings_pack::enable_natpmp, true);
    // Without bootstrap nodes a cold DHT can take a very long time to find
    // peers for a magnet, which is most of the "it just spins" complaint.
    sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
               "dht.libtorrent.org:25401,router.bittorrent.com:6881,"
               "router.utorrent.com:6881,dht.transmissionbt.com:6881");
    sp.set_int(lt::settings_pack::connections_limit, 500);
    sp.set_int(lt::settings_pack::active_limit, 40);
    sp.set_int(lt::settings_pack::alert_queue_size, 4000);
    return sp;
}

struct Engine {
    lt::session session{fastMetadataSettings()};
    std::mutex mutex;

    std::string savePath;
    std::string openMagnet;
    lt::torrent_handle handle;
    std::shared_ptr<const lt::torrent_info> info;
    std::vector<ScannedFile> files;

    int runtimeMinutes = 0;   // from AniList, for bitrate estimation
    int totalEpisodes = 0;    // from AniList, so the last one can complete the entry
    int anilistId = 0;
    // Where the next play should start: -1 means decide from the stored
    // position, 0 means the user chose to start over, anything else is an
    // explicit resume point in seconds.
    double resumeFrom = -1;
    std::string showTitle;
    std::string showCover;

    std::atomic<bool> playing{false};
    // True only while mpv is actually on screen. The page uses this to
    // switch to the control strip - keying off `playing` meant it did so
    // during buffering, while the window was still full-size.
    std::atomic<bool> videoActive{false};
    std::atomic<bool> stopRequested{false};
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

// Persisted preferences. Kept as plain JSON next to the WebView2 profile so
// it can be inspected or deleted by hand.

Settings g_settings;
std::mutex g_settingsMutex;

std::string settingsPath() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki" : std::string(".");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\settings.json";
}

json settingsToJson(const Settings& s) {
    return json{{"savePath", s.savePath},
                {"speedLimit", s.speedLimit},
                {"maxConnections", s.maxConnections},
                {"quality", s.quality},
                {"audioLang", s.audioLang},
                {"subLang", s.subLang},
                {"bufferSeconds", s.bufferSeconds},
                {"deleteAfter", s.deleteAfter},
                {"subsOn", s.subsOn},
                {"theme", s.theme},
                {"titleLanguage", s.titleLanguage},
                {"lookupPreference", s.lookupPreference},
                {"autoSelect", s.autoSelect},
                {"streamedDownload", s.streamedDownload},
                {"torrentPort", s.torrentPort},
                {"dhtPort", s.dhtPort},
                {"disableDHT", s.disableDHT},
                {"disablePeX", s.disablePeX},
                {"anilistClientId", s.anilistClientId},
                {"anilistClientSecret", s.anilistClientSecret},
                {"anilistRedirect", s.anilistRedirect},
                {"syncProgress", s.syncProgress},
                {"uiScale", s.uiScale},
                {"hideSpoilers", s.hideSpoilers},
                {"showAdult", s.showAdult},
                {"dohUrl", s.dohUrl},
                {"discordClientId", s.discordClientId},
                {"discordPresence", s.discordPresence}};
}

void settingsFromJson(const json& j, Settings& s) {
    if (j.contains("savePath") && j["savePath"].is_string()) s.savePath = j["savePath"];
    if (j.contains("speedLimit") && j["speedLimit"].is_number()) s.speedLimit = j["speedLimit"];
    if (j.contains("maxConnections") && j["maxConnections"].is_number()) s.maxConnections = j["maxConnections"];
    if (j.contains("quality") && j["quality"].is_string()) s.quality = j["quality"];
    if (j.contains("audioLang") && j["audioLang"].is_string()) s.audioLang = j["audioLang"];
    if (j.contains("subLang") && j["subLang"].is_string()) s.subLang = j["subLang"];
    if (j.contains("bufferSeconds") && j["bufferSeconds"].is_number()) s.bufferSeconds = j["bufferSeconds"];
    if (j.contains("deleteAfter") && j["deleteAfter"].is_boolean()) s.deleteAfter = j["deleteAfter"];
    if (j.contains("subsOn") && j["subsOn"].is_boolean()) s.subsOn = j["subsOn"];
    if (j.contains("theme") && j["theme"].is_string()) s.theme = j["theme"];
    if (j.contains("titleLanguage") && j["titleLanguage"].is_string()) s.titleLanguage = j["titleLanguage"];
    if (j.contains("lookupPreference") && j["lookupPreference"].is_string()) s.lookupPreference = j["lookupPreference"];
    if (j.contains("autoSelect") && j["autoSelect"].is_boolean()) s.autoSelect = j["autoSelect"];
    if (j.contains("streamedDownload") && j["streamedDownload"].is_boolean()) s.streamedDownload = j["streamedDownload"];
    if (j.contains("torrentPort") && j["torrentPort"].is_number()) s.torrentPort = j["torrentPort"];
    if (j.contains("dhtPort") && j["dhtPort"].is_number()) s.dhtPort = j["dhtPort"];
    if (j.contains("disableDHT") && j["disableDHT"].is_boolean()) s.disableDHT = j["disableDHT"];
    if (j.contains("disablePeX") && j["disablePeX"].is_boolean()) s.disablePeX = j["disablePeX"];
    if (j.contains("anilistClientId") && j["anilistClientId"].is_string()) s.anilistClientId = j["anilistClientId"];
    if (j.contains("anilistClientSecret") && j["anilistClientSecret"].is_string()) s.anilistClientSecret = j["anilistClientSecret"];
    if (j.contains("anilistRedirect") && j["anilistRedirect"].is_string() &&
        !j["anilistRedirect"].get<std::string>().empty()) {
        s.anilistRedirect = j["anilistRedirect"];
    }
    if (j.contains("syncProgress") && j["syncProgress"].is_boolean()) s.syncProgress = j["syncProgress"];
    if (j.contains("uiScale") && j["uiScale"].is_number()) s.uiScale = j["uiScale"];
    if (j.contains("hideSpoilers") && j["hideSpoilers"].is_boolean()) s.hideSpoilers = j["hideSpoilers"];
    if (j.contains("showAdult") && j["showAdult"].is_boolean()) s.showAdult = j["showAdult"];
    if (j.contains("dohUrl") && j["dohUrl"].is_string()) s.dohUrl = j["dohUrl"];
    if (j.contains("discordClientId") && j["discordClientId"].is_string()) s.discordClientId = j["discordClientId"];
    if (j.contains("discordPresence") && j["discordPresence"].is_boolean()) s.discordPresence = j["discordPresence"];
}

void loadSettings() {
    std::ifstream in(settingsPath());
    if (!in) return;
    try {
        json j;
        in >> j;
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        settingsFromJson(j, g_settings);
    } catch (const std::exception&) {
        // A corrupt settings file should not stop the app starting.
    }
}

void saveSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    std::ofstream out(settingsPath());
    if (out) out << settingsToJson(g_settings).dump(2) << "\n";
}

Settings currentSettings() {
    std::lock_guard<std::mutex> lock(g_settingsMutex);
    return g_settings;
}

std::string historyPath() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki" : std::string(".");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\history.json";
}

json loadHistory() {
    std::ifstream in(historyPath());
    if (!in) return json::array();
    try {
        json j;
        in >> j;
        if (j.is_array()) return j;
    } catch (const std::exception&) {
    }
    return json::array();
}

// Newest first, de-duplicated by magnet, capped so the file cannot grow
// without bound.
void rememberWatched(const json& entry) {
    json list = loadHistory();
    json next = json::array();
    next.push_back(entry);
    for (const auto& e : list) {
        // Same show, same episode is the same watch as far as anyone cares -
        // a second release of it is not a second row. Falls back to the magnet
        // when there is no AniList id to match on.
        const bool sameShow =
            entry.value("anilistId", 0) > 0 &&
            e.value("anilistId", 0) == entry.value("anilistId", 0) &&
            e.value("episode", -1) == entry.value("episode", -2);
        const bool sameFile = e.value("magnet", "") == entry.value("magnet", "") &&
                              e.value("file", "") == entry.value("file", "");
        if (sameShow || sameFile) continue;
        next.push_back(e);
        if (next.size() >= 40) break;
    }
    std::ofstream out(historyPath());
    if (out) out << next.dump(2) << "\n";
}

// Linking has to happen in the real browser: that is where the AniList
// session already is, and it is the only place the user can see what they are
// approving.
void openExternal(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    std::system(("xdg-open " + url).c_str());
#endif
}

void* g_videoHost = nullptr;
PlaybackHook g_playbackHook = nullptr;

Engine& engine() {
    static Engine e;
    return e;
}

// Rate and connection limits can change while the session is running.
void applySessionSettings(Engine& e, const Settings& cfg) {
    lt::settings_pack sp;
    const int bytes = cfg.speedLimit > 0
                          ? static_cast<int>(cfg.speedLimit * 1000000.0 / 8.0)
                          : 0;  // libtorrent treats 0 as unlimited
    sp.set_int(lt::settings_pack::download_rate_limit, bytes);
    sp.set_int(lt::settings_pack::upload_rate_limit, bytes);
    sp.set_int(lt::settings_pack::connections_limit,
               cfg.maxConnections > 0 ? cfg.maxConnections : 200);

    // Private trackers want these off; they also cut peer discovery hard,
    // which is why they are opt-in rather than defaults.
    sp.set_bool(lt::settings_pack::enable_dht, !cfg.disableDHT);
    sp.set_bool(lt::settings_pack::enable_lsd, !cfg.disableDHT);

    if (cfg.torrentPort > 0) {
        sp.set_str(lt::settings_pack::listen_interfaces,
                   "0.0.0.0:" + std::to_string(cfg.torrentPort) + ",[::]:" +
                       std::to_string(cfg.torrentPort));
    }
    if (cfg.dhtPort > 0) {
        sp.set_str(lt::settings_pack::dht_bootstrap_nodes,
                   "dht.libtorrent.org:25401,router.bittorrent.com:6881,"
                   "router.utorrent.com:6881,dht.transmissionbt.com:6881");
    }
    e.session.apply_settings(sp);

    http::setDohUrl(cfg.dohUrl);
    if (cfg.discordPresence && !cfg.discordClientId.empty()) {
        discord::connect(cfg.discordClientId);
    } else {
        discord::clear();
        discord::disconnect();
    }
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
    if (currentSettings().disablePeX) atp.flags |= lt::torrent_flags::disable_pex;

    // Leaving a torrent takes its remaining data with it - downloads are
    // disposable, so nothing should survive switching releases.
    if (e.handle.is_valid()) {
        e.session.remove_torrent(e.handle, lt::session::delete_files);
        e.handle = lt::torrent_handle();
        e.info.reset();
        e.openMagnet.clear();
    }
    e.handle = e.session.add_torrent(std::move(atp));

    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + std::chrono::seconds(90);
    while (!e.handle.status().has_metadata) {
        if (std::chrono::steady_clock::now() > deadline) {
            err = "Gave up after 90s fetching torrent metadata - no peers responded. "
                  "Try a release with more seeders.";
            e.setMessage("");
            return false;
        }
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::steady_clock::now() - started).count();
        e.setMessage("Fetching torrent metadata - " +
                     std::to_string(e.handle.status().num_peers) + " peers, " +
                     std::to_string(secs) + "s");
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    e.setMessage("");

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
    e.showTitle.clear();
    e.showCover.clear();
    e.anilistId = 0;
    e.runtimeMinutes = 0;
    e.totalEpisodes = 0;

    // Work out which show this is from the release name, rather than relying
    // on the page to have carried an id here. Opening from history, or from a
    // pasted magnet, used to leave anilistId at 0 - which silently disabled
    // both progress sync and per-show resume.
    {
        std::string guess;
        for (const auto& f : e.files) {
            if (!f.excluded && !f.title.empty()) {
                guess = f.title;
                break;
            }
        }
        if (!guess.empty()) {
            const auto matches = anilist::search(guess);
            if (!matches.empty()) {
                e.anilistId = matches.front().id;
                e.showTitle = matches.front().preferred;
            }
        }
    }

    if (e.files.empty()) {
        err = "No video files in this torrent.";
        return false;
    }
    return true;
}

// Buffers head+tail then hands the file to mpv. Runs on its own thread so the
// UI stays responsive.
void playFile(Engine& e, int index) {
    e.stopRequested = false;
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

    // The tail must land before any player can parse the container.
    constexpr int kTailPieces = 4;

    const lt::file_index_t fidx{chosen->index};
    const int pieceLen = e.info->piece_length();
    const int firstPiece = static_cast<int>(e.info->map_file(fidx, 0, 0).piece);
    const int lastPiece = static_cast<int>(
        e.info->map_file(fidx, std::max<std::int64_t>(chosen->size - 1, 0), 0).piece);
    // ---- adaptive pre-buffer -------------------------------------------
    //
    // A fixed buffer is either wasteful on a fast swarm or too small on a slow
    // one. Size it from the numbers we can actually measure: the file's bitrate
    // and the rate we are pulling it at.
    const double durationSec = e.runtimeMinutes > 0 ? e.runtimeMinutes * 60.0 : 24 * 60.0;
    const double bitrate = static_cast<double>(chosen->size) / durationSec;  // bytes/sec

    // The tail must land before any player can parse the container, and a few
    // head pieces before it can start decoding. Fetch that much first, and use
    // the wait to measure throughput.
    std::vector<int> priming;
    const int primeHead = std::max(1, static_cast<int>((4 * 1024 * 1024 + pieceLen - 1) / pieceLen));
    for (int p = firstPiece; p <= std::min(firstPiece + primeHead - 1, lastPiece); ++p) {
        priming.push_back(p);
    }
    for (int p = std::max(lastPiece - kTailPieces + 1, firstPiece); p <= lastPiece; ++p) {
        if (std::find(priming.begin(), priming.end(), p) == priming.end()) priming.push_back(p);
    }
    for (const int p : priming) {
        e.handle.piece_priority(lt::piece_index_t{p}, lt::top_priority);
        e.handle.set_piece_deadline(lt::piece_index_t{p}, 0);
    }

    const auto measureStart = std::chrono::steady_clock::now();
    const std::int64_t startBytes = e.handle.status().total_done;
    for (;;) {
        int have = 0;
        for (const int p : priming) {
            if (e.handle.have_piece(lt::piece_index_t{p})) ++have;
        }
        const int pct = priming.empty() ? 100 : (have * 100 / static_cast<int>(priming.size()));
        e.progress = pct / 3;
        e.setMessage("Preparing stream " + std::to_string(pct) + "% (" +
                     std::to_string(e.handle.status().num_peers) + " peers)");
        if (have == static_cast<int>(priming.size())) break;
        if (e.stopRequested) { e.done = true; e.playing = false; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    const double elapsed = std::max(
        0.5, std::chrono::duration<double>(std::chrono::steady_clock::now() - measureStart).count());
    const double rate = std::max(1.0, static_cast<double>(e.handle.status().total_done - startBytes) / elapsed);

    // If we are outpacing the video we only need a courtesy buffer. If we are
    // behind, pre-load enough to cover the shortfall across the whole runtime,
    // because that gap never closes on its own.
    const Settings cfg = currentSettings();

    double bufferSeconds = 8.0;
    if (cfg.streamedDownload) {
        // Fetch only what playback is about to need. Gentler on the swarm and
        // on disk, at the cost of stalling if the connection dips.
        bufferSeconds = 6.0;
    } else if (cfg.bufferSeconds > 0) {
        // Explicit override from settings wins over the measurement.
        bufferSeconds = cfg.bufferSeconds;
    } else if (rate < bitrate) {
        const double deficit = (bitrate - rate) / bitrate;  // fraction we cannot keep up with
        bufferSeconds = std::min(durationSec * deficit + 10.0, durationSec * 0.9);
    }
    const std::int64_t bufferBytes =
        std::min<std::int64_t>(static_cast<std::int64_t>(bufferSeconds * bitrate), chosen->size);
    const int bufferPieces = std::max(
        1, static_cast<int>((bufferBytes + pieceLen - 1) / pieceLen));

    e.setMessage("Buffering " + std::to_string(static_cast<int>(bufferSeconds)) + "s at " +
                 std::to_string(static_cast<int>(rate / 1024 / 1024 * 8)) + " Mb/s");

    for (int p = firstPiece; p <= std::min(firstPiece + bufferPieces - 1, lastPiece); ++p) {
        e.handle.piece_priority(lt::piece_index_t{p}, lt::top_priority);
        e.handle.set_piece_deadline(lt::piece_index_t{p}, 0);
    }
    for (;;) {
        int have = 0;
        const int last = std::min(firstPiece + bufferPieces - 1, lastPiece);
        for (int p = firstPiece; p <= last; ++p) {
            if (e.handle.have_piece(lt::piece_index_t{p})) ++have;
        }
        const int total = last - firstPiece + 1;
        const int pct = total <= 0 ? 100 : (have * 100 / total);
        e.progress = 33 + pct * 2 / 3;
        e.setMessage("Buffering " + std::to_string(pct) + "% - " +
                     std::to_string(static_cast<int>(bufferSeconds)) + "s ahead, " +
                     std::to_string(e.handle.status().num_peers) + " peers");
        if (have >= total) break;
        if (e.stopRequested) { e.done = true; e.playing = false; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
    }

    e.progress = 100;
    e.setMessage("Playing");

    const std::string path = e.savePath + "/" + chosen->path;

    // Resume where this episode was left, if it was left anywhere useful.
    const int watchedEp = chosen->episode.valid && !chosen->episode.isRange()
                              ? chosen->episode.from : 0;
    // to_string() on a sha1_hash is twenty raw bytes, nulls and all - fine as
    // a hash, fatal as a JSON key. Hex it.
    std::string infoHex;
    {
        const lt::sha1_hash h = e.info->info_hashes().get_best();
        static const char* digits = "0123456789abcdef";
        for (const unsigned char b : h) {
            infoHex.push_back(digits[b >> 4]);
            infoHex.push_back(digits[b & 0x0F]);
        }
    }
    const std::string progressKey =
        library::keyFor(e.anilistId, watchedEp, infoHex, chosen->index);
    const library::EpisodeProgress resume = library::get(progressKey);

    // Everything the record needs to be reopened straight from the home
    // screen, filled in once rather than at every checkpoint.
    const auto record = [&](double seconds, double duration, bool completed) {
        library::EpisodeProgress p;
        p.anilistId = e.anilistId;
        p.episode = watchedEp;
        p.currentTime = seconds;
        p.duration = duration;
        p.completed = completed;
        p.title = e.showTitle.empty() ? e.info->name() : e.showTitle;
        p.cover = e.showCover;
        p.magnet = e.openMagnet;
        p.file = chosen->name;
        p.infoHash = infoHex;
        p.fileIndex = chosen->index;
        library::put(p);
    };

    std::string cmd = "\"" + findMpv() + "\"";
    if (g_videoHost) {
        // mpv draws straight into our window, so there is no second window and
        // no taskbar entry - it looks and behaves like a built-in player.
        cmd += " --wid=" + std::to_string(reinterpret_cast<std::uintptr_t>(g_videoHost));
        cmd += " --no-border --osc=yes --keep-open=no";
    }
    cmd += " --input-ipc-server=" + player::pipeName();
    if (!cfg.audioLang.empty()) cmd += " --alang=" + cfg.audioLang;
    if (!cfg.subLang.empty()) cmd += " --slang=" + cfg.subLang;
    cmd += cfg.subsOn ? " --sub-visibility=yes" : " --sub-visibility=no";
    if (e.resumeFrom >= 0 ? e.resumeFrom > 0 : library::worthResuming(resume)) {
        const double at = e.resumeFrom >= 0 ? e.resumeFrom : resume.currentTime;
        cmd += " --start=" + std::to_string(static_cast<long long>(at));
        e.setMessage("Resuming at " + std::to_string(static_cast<int>(at) / 60) + "m" +
                     std::to_string(static_cast<int>(at) % 60) + "s");
    }
    cmd += " --force-window=yes --cache=yes --demuxer-max-bytes=200MiB";
    cmd += " \"" + path + "\"";

    {
        json entry{{"magnet", e.openMagnet},
                   {"file", chosen->name},
                   {"torrent", e.info->name()},
                   {"show", e.showTitle},
                   {"cover", e.showCover},
                   {"anilistId", e.anilistId},
                   {"episode", chosen->episode.valid ? chosen->episode.from : 0},
                   {"at", static_cast<long long>(std::time(nullptr))}};
        rememberWatched(entry);
    }

    e.videoActive = true;
    // Belt and braces: if the file is missing or empty, say so rather than
    // launching a player onto nothing and looking like a glitch.
    {
        std::error_code ec;
        const auto onDisk = std::filesystem::file_size(path, ec);
        if (ec || onDisk == 0) {
            e.setMessage("That episode is no longer on disk. Pick it again to re-download.");
            e.done = true;
            e.playing = false;
            return;
        }
    }

    if (g_playbackHook) g_playbackHook(true);

    // CreateProcess rather than system(): we need to stay alive alongside mpv
    // to keep feeding it pieces ahead of the playhead.
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back('\0');

    if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi)) {
        e.videoActive = false;
        if (g_playbackHook) g_playbackHook(false);
        e.setMessage("Could not start mpv. Is it installed?");
        e.done = true;
        e.playing = false;
        return;
    }

    // ---- rolling window ------------------------------------------------
    //
    // Follow the playhead and keep a window of pieces ahead of it at top
    // priority, instead of downloading the file front-to-back and hoping.
    // Position comes from mpv itself over the IPC socket.
    const int windowPieces = std::max(2, bufferPieces);
    int lastWindowStart = firstPiece;
    double lastPosition = 0, lastDuration = 0, lastSaved = 0;
    double lastSample = 0;
    bool wasPaused = false, sampled = false;
    // A silent IPC failure used to look exactly like a normal watch: the video
    // played, and nothing else worked. Notice it and say so instead.
    const auto playbackStart = std::chrono::steady_clock::now();
    bool everConnected = false, warnedAboutIpc = false;

    while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT) {
        if (e.stopRequested) {
            player::stop();
            break;
        }

        const player::State ps = player::state();
        if (!ps.running) {
            const auto waited = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::steady_clock::now() - playbackStart).count();
            if (!everConnected && !warnedAboutIpc && waited >= 15) {
                warnedAboutIpc = true;
                e.setMessage("Playing, but mpv is not answering on its control socket - "
                             "controls, resume and progress sync are off for this episode.");
            }
            continue;
        }
        everConnected = true;

        // mpv reports no duration until it has parsed enough of the container,
        // and for a few files never does. Fall back to the runtime AniList
        // knows about rather than abandoning the episode's progress outright.
        const double duration = ps.duration > 0 ? ps.duration : durationSec;
        lastPosition = ps.position;
        lastDuration = duration;

        // Checkpoint on a five second tick, and immediately on the two things
        // that mean the position just changed for a reason: pausing, and
        // seeking. A seek is a jump the wall clock cannot account for - the
        // loop runs twice a second, so anything past three seconds of movement
        // in one iteration was the user, not playback.
        const bool paused = ps.paused && !wasPaused;
        const bool seeked = sampled && std::fabs(ps.position - lastSample) > 3.0;
        const bool ticked = lastSaved == 0 || std::fabs(ps.position - lastSaved) >= 5.0;
        wasPaused = ps.paused;
        lastSample = ps.position;
        sampled = true;

        if (ticked || paused || seeked) {
            lastSaved = ps.position;
            record(ps.position, duration, false);
        }

        const double fraction = std::min(1.0, ps.position / duration);
        const std::int64_t offset = static_cast<std::int64_t>(fraction * chosen->size);
        const int atPiece = static_cast<int>(
            e.info->map_file(fidx, std::min(offset, chosen->size - 1), 0).piece);

        if (atPiece != lastWindowStart) {
            lastWindowStart = atPiece;
            const int windowEnd = std::min(atPiece + windowPieces, lastPiece);
            for (int p = atPiece; p <= windowEnd; ++p) {
                if (e.handle.have_piece(lt::piece_index_t{p})) continue;
                e.handle.piece_priority(lt::piece_index_t{p}, lt::top_priority);
                // Nearer pieces get tighter deadlines so libtorrent orders
                // requests by how soon each one is actually needed.
                e.handle.set_piece_deadline(lt::piece_index_t{p}, (p - atPiece) * 500);
            }
        }

        int ahead = 0;
        for (int p = atPiece; p <= std::min(atPiece + windowPieces, lastPiece); ++p) {
            if (!e.handle.have_piece(lt::piece_index_t{p})) break;
            ++ahead;
        }
        const double secsAhead = bitrate > 0 ? (double)ahead * pieceLen / bitrate : 0;
        e.setMessage(ps.paused ? "Paused" : "Playing - " + std::to_string((int)secsAhead) +
                                                "s buffered");
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    e.videoActive = false;
    discord::clear();
    if (g_playbackHook) g_playbackHook(false);

    // hayase-app/interface player.svelte: fromend = max(180, duration/10), and
    // anything past that counts as watched. Better than a flat percentage -
    // it means skipping the ending still finishes the episode.
    const double fromEnd = lastDuration > 0
                               ? std::max(180.0, lastDuration / 10.0)
                               : 0.0;
    const bool finished = lastDuration > 0 && (lastDuration - fromEnd) < lastPosition;
    if (lastDuration > 0) {
        // Marked finished rather than deleted: Continue Watching filters
        // completed episodes out anyway, and keeping the row is what stops a
        // replayed completion from looking like a brand new watch.
        record(lastPosition, lastDuration, finished);
    }

    const int watchedEpisode = watchedEp;
    if (cfg.syncProgress && e.anilistId > 0 && watchedEpisode > 0 && finished) {
        // Handed to the sync queue rather than sent from here. The write lands
        // on disk first, the interface moves on immediately, and the worker
        // retries on its own if AniList is unreachable - so finishing an
        // episode on a dead connection still counts.
        const int total = e.totalEpisodes;
        library::recordWatched(e.anilistId, watchedEpisode, total);
        e.setMessage("Episode " + std::to_string(watchedEpisode) + " watched" +
                     (total > 0 && watchedEpisode >= total ? " - series complete" : ""));
    }

    // Remove only the episode just watched, and keep the torrent open. Tearing
    // the whole torrent down here meant the file list on screen pointed at
    // something that no longer existed, so picking a second episode from the
    // same batch failed with "that file is no longer available".
    e.setMessage("Finished. Removing that episode...");
    e.handle.file_priority(fidx, lt::dont_download);

    // libtorrent may still hold the handle for a moment after the priority
    // drop, so give the delete a few attempts before reporting it stuck.
    bool removed = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        std::error_code ec;
        if (std::filesystem::remove(path, ec) || !std::filesystem::exists(path, ec)) {
            removed = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    if (!cfg.deleteAfter) {
        e.setMessage("Finished. Kept on disk (delete-after-watching is off).");
        e.done = true;
        e.playing = false;
        return;
    }

    if (removed) {
        // Deleting behind libtorrent's back leaves its piece bitfield claiming
        // the data is still present, so replaying the episode skipped
        // buffering entirely and handed mpv a file that no longer existed.
        // A recheck is cheap here precisely because almost nothing is on disk.
        e.setMessage("Episode removed - refreshing torrent state...");
        e.handle.force_recheck();

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (std::chrono::steady_clock::now() < deadline) {
            const lt::torrent_status st = e.handle.status();
            if (st.state != lt::torrent_status::checking_files &&
                st.state != lt::torrent_status::checking_resume_data) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    e.setMessage(removed ? "Episode removed. Pick another one."
                         : "Finished, but that file could not be deleted yet - "
                           "it will go when you close Tsuzuki.");
    e.done = true;
    e.playing = false;
}


}  // namespace

Settings settings() { return currentSettings(); }

void applySettings(const Settings& next) {
    {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        g_settings = next;
    }
    saveSettings();

    Engine& e = engine();
    const Settings cfg = currentSettings();
    applySessionSettings(e, cfg);
    // A new download folder applies to the next torrent, not the open one.
    if (!cfg.savePath.empty() && !e.handle.is_valid()) e.savePath = cfg.savePath;
}

// Defined below, with the rest of the host hooks.
extern AuthHook g_authHook;

bool startAniListLogin(std::string& error) {
    const Settings cfg = currentSettings();
    const std::string clientId = track::resolveClientId(cfg.anilistClientId);

    // In the app, use the window we control and the implicit grant - no
    // redirect handling and nothing to copy.
    if (g_authHook) {
        const std::string inApp = track::implicitAuthorizeUrl(clientId);
        if (!inApp.empty()) {
            g_authHook(inApp.c_str());
            return true;
        }
    }

    const std::string url = track::authorizeUrl(clientId, cfg.anilistRedirect);
    if (url.empty()) {
        error =
            "No AniList client id is available. Add one in Settings, or "
            "rebuild with one baked in.";
        return false;
    }
    openExternal(url);
    return true;
}

void logoutAniList() { track::logout(); }

std::vector<HistoryItem> history() {
    std::vector<HistoryItem> out;
    for (const auto& e : loadHistory()) {
        if (!e.is_object()) continue;
        HistoryItem h;
        h.magnet = e.value("magnet", "");
        h.file = e.value("file", "");
        h.torrent = e.value("torrent", "");
        h.show = e.value("show", "");
        h.cover = e.value("cover", "");
        h.anilistId = e.value("anilistId", 0);
        h.episode = e.value("episode", 0);
        h.at = e.value("at", 0LL);
        out.push_back(std::move(h));
    }
    return out;
}

void shutdown() {
    discord::clear();
    discord::disconnect();
    // Before anything that can block: the queue and the watch database must
    // survive the close even if tearing the torrent down goes wrong.
    library::stop();

    Engine& e = engine();
    std::lock_guard<std::mutex> lock(e.mutex);
    if (!e.handle.is_valid()) return;

    e.session.remove_torrent(e.handle, lt::session::delete_files);
    e.handle = lt::torrent_handle();
    e.info.reset();
    e.openMagnet.clear();

    // Wait for the delete to be confirmed rather than assuming it happened -
    // a delete racing the player is exactly how data gets left behind.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<lt::alert*> alerts;
        e.session.pop_alerts(&alerts);
        bool settled = false;
        for (const lt::alert* a : alerts) {
            if (lt::alert_cast<lt::torrent_deleted_alert>(a) ||
                lt::alert_cast<lt::torrent_delete_failed_alert>(a)) {
                settled = true;
            }
        }
        if (settled) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

AuthHook g_authHook = nullptr;

void setAuthHook(AuthHook hook) { g_authHook = hook; }

void acceptToken(const std::string& token) {
    if (!token.empty()) track::setToken(token);
}

void setVideoHost(void* hwnd, PlaybackHook hook) {
    g_videoHost = hwnd;
    g_playbackHook = hook;
}

static void installRoutes(httplib::Server& server, Engine& e) {
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

        const Settings pre = currentSettings();
        auto matches = anilist::search(q);
        if (!pre.showAdult) {
            matches.erase(std::remove_if(matches.begin(), matches.end(),
                                         [](const anilist::Media& m) { return m.isAdult; }),
                          matches.end());
        }
        if (!matches.empty()) {
            const auto& m = matches.front();
            query.title = m.preferred.empty() ? q : m.preferred;
            query.anilistId = m.id;
            reply["anilist"] = {{"title", query.title}, {"episodes", m.episodes}, {"id", m.id}};
        }

        const std::vector<std::shared_ptr<sources::Source>> all = {
            sources::makeSeaDex(), sources::makeAnimeTosho(), sources::makeNyaa(),
            sources::makeSubsPlease(),
        };
        auto results = sources::searchAll(all, query);

        // Lookup preference re-orders what searchAll produced. Curated picks
        // stay first regardless - that is the one signal worth more than any
        // of these heuristics.
        const Settings cfg = currentSettings();
        if (cfg.lookupPreference == "size") {
            std::stable_sort(results.begin(), results.end(),
                             [](const sources::Result& a, const sources::Result& b) {
                                 if (a.curatedBest != b.curatedBest) return a.curatedBest;
                                 if (a.size == 0 || b.size == 0) return a.size > b.size;
                                 return a.size < b.size;
                             });
        } else if (cfg.lookupPreference == "availability") {
            std::stable_sort(results.begin(), results.end(),
                             [](const sources::Result& a, const sources::Result& b) {
                                 if (a.curatedBest != b.curatedBest) return a.curatedBest;
                                 return a.seeders > b.seeders;
                             });
        }
        reply["autoSelect"] = cfg.autoSelect;

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

        // Artwork and blurb, so the file list is not a bare filename dump.
        const int wantId = in.contains("anilistId") && in["anilistId"].is_number_integer() &&
                                   in["anilistId"].get<int>() > 0
                               ? in["anilistId"].get<int>()
                               : e.anilistId;
        if (wantId > 0) {
            anilist::Details d;
            if (anilist::details(wantId, d)) {
                reply["show"] = {
                    {"title", d.title},       {"description", d.description},
                    {"cover", d.coverImage},  {"banner", d.bannerImage},
                    {"color", d.color},       {"duration", d.duration},
                    {"episodes", d.episodes},
                };
                json eps = json::object();
                for (const auto& ei : d.episodeInfo) {
                    eps[std::to_string(ei.number)] = {{"title", ei.title},
                                                     {"thumb", ei.thumbnail}};
                }
                reply["episodeInfo"] = eps;
                e.runtimeMinutes = d.duration;
                e.totalEpisodes = d.episodes;
                e.anilistId = d.id;
                e.showTitle = d.title;
                e.showCover = d.coverImage;
            }
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
            // -1 keeps the stored position; the page sends 0 for "start over"
            // and a number of seconds for "resume".
            e.resumeFrom = in.contains("resumeFrom") && in["resumeFrom"].is_number()
                               ? in["resumeFrom"].get<double>()
                               : -1;
            std::thread(
                [&e, index] {
                    std::lock_guard<std::mutex> lock(e.mutex);
                    playFile(e, index);
                })
                .detach();
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    // AniList redirects here with ?code=. Unlike a fragment, a query string
    // reaches the server, so the exchange happens here rather than in script.
    server.Get("/auth/anilist", [](const httplib::Request& req, httplib::Response& res) {
        const Settings cfg = currentSettings();
        std::string error;
        bool ok = false;

        // Visiting this page directly - no code, no error - is not a failure.
        // It only means someone typed the URL in, so say so plainly instead of
        // announcing that linking broke.
        if (!req.has_param("code") && !req.has_param("error")) {
            res.set_content(
                "<!doctype html><meta charset=utf-8><title>Tsuzuki</title>"
                "<style>body{background:#0e0d17;color:#9a94bd;font:15px system-ui;"
                "display:flex;align-items:center;justify-content:center;height:100vh;"
                "margin:0;text-align:center}b{color:#ece9f7}</style>"
                "<div><p><b>This is Tsuzuki&rsquo;s login callback.</b><br>"
                "Nothing to do here - it is only somewhere AniList sends you back to.</p>"
                "<p style=\"font-size:13px\">Start linking from Settings &rarr; Account "
                "inside Tsuzuki.</p></div>",
                "text/html; charset=utf-8");
            return;
        }

        if (req.has_param("error")) {
            error = req.get_param_value("error_description").empty()
                        ? req.get_param_value("error")
                        : req.get_param_value("error_description");
        } else {
            ok = track::exchangeCode(req.has_param("code") ? req.get_param_value("code") : "",
                                     track::resolveClientId(cfg.anilistClientId),
                                     track::resolveClientSecret(cfg.anilistClientSecret),
                                     cfg.anilistRedirect, error);
        }

        const std::string body =
            std::string("<!doctype html><meta charset=utf-8><title>Tsuzuki</title>"
                        "<style>body{background:#0e0d17;color:#ece9f7;font:15px system-ui;"
                        "display:flex;align-items:center;justify-content:center;height:100vh;"
                        "margin:0;text-align:center}b{color:#ff5c8d}"
                        "code{background:#1f1c34;padding:3px 7px;border-radius:5px;"
                        "color:#5be9e9;font-size:13px}</style><div>") +
            (ok ? "<p><b>Account linked.</b><br>You can close this tab and go back to Tsuzuki.</p>"
                : "<p><b>Linking failed.</b><br>" + error +
                      "</p><p style='color:#9a94bd;font-size:13px'>Most often the Redirect URL on "
                      "your AniList client does not match<br><code>http://127.0.0.1:7654/auth/anilist</code></p>") +
            "</div>";
        res.set_content(body, "text/html; charset=utf-8");
    });

    server.Post("/api/account/token", [](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const std::exception&) {
            res.set_content("{\"ok\":false}", "application/json");
            return;
        }
        const std::string tok = in.value("token", "");
        if (!tok.empty()) track::setToken(tok);
        res.set_content(json{{"ok", !tok.empty()}}.dump(), "application/json");
    });

    server.Get("/api/account", [](const httplib::Request&, httplib::Response& res) {
        const track::Account a = track::account();
        const Settings cfg = currentSettings();
        res.set_content(json{{"linked", a.linked},
                             {"name", a.name},
                             {"avatar", a.avatar},
                             {"hasClientId", !track::resolveClientId(cfg.anilistClientId).empty()},
                             {"usingBuiltInId", cfg.anilistClientId.empty() &&
                                                    !track::defaultClientId().empty()},
                             {"clientId", track::resolveClientId(cfg.anilistClientId)},
                             {"hasClientSecret",
                              !track::resolveClientSecret(cfg.anilistClientSecret).empty()},
                             {"syncProgress", cfg.syncProgress}}
                            .dump(),
                        "application/json");
    });

    server.Post("/api/account/login", [](const httplib::Request&, httplib::Response& res) {
        const Settings cfg = currentSettings();
        // In the app, use the window we control and the implicit grant - no
        // redirect handling, nothing to copy. The browser hand-off remains for
        // the console build.
        if (g_authHook) {
            const std::string inApp =
                track::implicitAuthorizeUrl(track::resolveClientId(cfg.anilistClientId));
            if (!inApp.empty()) {
                g_authHook(inApp.c_str());
                res.set_content(json{{"ok", true}, {"inApp", true}}.dump(), "application/json");
                return;
            }
        }

        const std::string url =
            track::authorizeUrl(track::resolveClientId(cfg.anilistClientId), cfg.anilistRedirect);
        if (url.empty()) {
            res.set_content(
                json{{"ok", false},
                     {"error",
                      "No AniList client id is available. Add one in Settings, "
                      "or rebuild with one baked in."}}
                    .dump(),
                "application/json");
            return;
        }
        openExternal(url);
        res.set_content(json{{"ok", true}, {"url", url}}.dump(), "application/json");
    });

    // Accepts either a bare code or the entire URL the browser landed on, so
    // a redirect we cannot listen on still completes the link.
    server.Post("/api/account/code", [](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const std::exception&) {
            res.set_content("{\"ok\":false,\"error\":\"bad request\"}", "application/json");
            return;
        }

        std::string code = in.value("code", "");
        const auto at = code.find("code=");
        if (at != std::string::npos) {
            code = code.substr(at + 5);
            const auto amp = code.find_first_of("&# ");
            if (amp != std::string::npos) code = code.substr(0, amp);
        }

        const Settings cfg = currentSettings();
        std::string error;
        const bool ok = track::exchangeCode(code, track::resolveClientId(cfg.anilistClientId),
                                            track::resolveClientSecret(cfg.anilistClientSecret),
                                            cfg.anilistRedirect, error);
        res.set_content(json{{"ok", ok}, {"error", error}}.dump(), "application/json");
    });

    server.Post("/api/account/logout", [](const httplib::Request&, httplib::Response& res) {
        track::logout();
        res.set_content("{\"ok\":true}", "application/json");
    });

    server.Get("/api/browse", [](const httplib::Request& req, httplib::Response& res) {
        anilist::BrowseFilters f;
        const Settings cfg = currentSettings();
        f.allowAdult = cfg.showAdult;
        if (req.has_param("q")) f.search = req.get_param_value("q");
        if (req.has_param("genre")) f.genre = req.get_param_value("genre");
        if (req.has_param("season")) f.season = req.get_param_value("season");
        if (req.has_param("format")) f.format = req.get_param_value("format");
        if (req.has_param("status")) f.status = req.get_param_value("status");
        if (req.has_param("sort")) f.sort = req.get_param_value("sort");
        if (req.has_param("year")) f.year = std::atoi(req.get_param_value("year").c_str());
        if (req.has_param("page")) f.page = std::atoi(req.get_param_value("page").c_str());

        json out = json::array();
        for (const auto& b : anilist::browse(f)) {
            out.push_back({{"id", b.id},
                           {"title", b.title},
                           {"cover", b.cover},
                           {"color", b.color},
                           {"episodes", b.episodes},
                           {"year", b.year},
                           {"score", b.score},
                           {"format", b.format},
                           {"status", b.status},
                           {"genres", b.genres}});
        }
        res.set_content(out.dump(), "application/json");
    });

    server.Get("/api/airing", [](const httplib::Request& req, httplib::Response& res) {
        const int days = req.has_param("days")
                             ? std::atoi(req.get_param_value("days").c_str()) : 7;
        json out = json::array();
        for (const auto& a : anilist::airing(days > 0 ? days : 7)) {
            out.push_back({{"mediaId", a.mediaId},
                           {"episode", a.episode},
                           {"airingAt", a.airingAt},
                           {"title", a.title},
                           {"cover", a.cover},
                           {"color", a.color}});
        }
        res.set_content(out.dump(), "application/json");
    });

    server.Get("/api/genres", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json(anilist::genres()).dump(), "application/json");
    });

    server.Get("/api/resume", [](const httplib::Request& req, httplib::Response& res) {
        const int id = req.has_param("anilistId")
                           ? std::atoi(req.get_param_value("anilistId").c_str()) : 0;
        const int ep = req.has_param("episode")
                           ? std::atoi(req.get_param_value("episode").c_str()) : 0;
        const library::EpisodeProgress p = library::get(library::keyFor(id, ep, "", -1));
        res.set_content(json{{"known", p.known},
                             {"episode", p.episode},
                             {"seconds", p.currentTime},
                             {"duration", p.duration},
                             {"percent", p.percent()},
                             {"remaining", p.remaining()},
                             {"completed", p.completed},
                             {"resumable", library::worthResuming(p)}}
                            .dump(),
                        "application/json");
    });

    // Answered from the local cache, so the home screen paints immediately
    // even on a cold start or with no connection. The worker refreshes it in
    // the background and the page picks the new values up on its next poll.
    server.Get("/api/lists", [](const httplib::Request&, httplib::Response& res) {
        json out = json::array();
        for (const auto& e : library::cachedList()) {
            out.push_back({{"mediaId", e.mediaId},
                           {"progress", e.progress},
                           {"episodes", e.episodes},
                           {"nextEpisode", e.nextEpisode},
                           {"status", e.status},
                           {"title", e.title},
                           {"cover", e.cover},
                           {"color", e.color},
                           {"airing", e.airing}});
        }
        res.set_content(out.dump(), "application/json");
    });

    // Part-watched episodes, newest first, with everything the card needs to
    // draw a progress bar and reopen the episode at the right second.
    server.Get("/api/continue", [](const httplib::Request&, httplib::Response& res) {
        json out = json::array();
        for (const auto& p : library::continueWatching(12)) {
            out.push_back({{"anilistId", p.anilistId},
                           {"episode", p.episode},
                           {"currentTime", p.currentTime},
                           {"duration", p.duration},
                           {"percent", p.percent()},
                           {"remaining", p.remaining()},
                           {"lastWatchedAt", p.lastWatchedAt},
                           {"title", p.title},
                           {"cover", p.cover},
                           {"magnet", p.magnet},
                           {"file", p.file}});
        }
        res.set_content(out.dump(), "application/json");
    });

    server.Get("/api/sync", [](const httplib::Request&, httplib::Response& res) {
        const library::SyncStatus st = library::syncStatus();
        res.set_content(json{{"label", st.label()},
                             {"pending", st.pending},
                             {"lastSyncAt", st.lastSyncAt},
                             {"lastError", st.lastError},
                             {"linked", st.state != library::SyncState::NotLinked}}
                            .dump(),
                        "application/json");
    });

    server.Post("/api/sync/refresh", [](const httplib::Request&, httplib::Response& res) {
        library::refreshFromAniList();
        res.set_content("{\"ok\":true}", "application/json");
    });

    server.Get("/api/history", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(loadHistory().dump(), "application/json");
    });

    server.Get("/api/settings", [&e](const httplib::Request&, httplib::Response& res) {
        Settings cfg = currentSettings();
        if (cfg.savePath.empty()) cfg.savePath = e.savePath;
        res.set_content(settingsToJson(cfg).dump(), "application/json");
    });

    server.Post("/api/settings", [&e](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const std::exception&) {
            res.set_content("{\"error\":\"bad request\"}", "application/json");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            settingsFromJson(in, g_settings);
        }
        saveSettings();

        const Settings cfg = currentSettings();
        applySessionSettings(e, cfg);
        // A new download folder applies to the next torrent, not the open one.
        if (!cfg.savePath.empty() && !e.handle.is_valid()) e.savePath = cfg.savePath;

        res.set_content(settingsToJson(cfg).dump(), "application/json");
    });

    server.Get("/api/player/state", [&e](const httplib::Request&, httplib::Response& res) {
        const player::State ps = player::state();
        json tracks = json::array();
        for (const auto& t : ps.tracks) {
            tracks.push_back({{"id", t.id},
                              {"type", t.type},
                              {"title", t.title},
                              {"lang", t.lang},
                              {"codec", t.codec},
                              {"selected", t.selected},
                              {"default", t.isDefault}});
        }
        res.set_content(json{{"running", ps.running},
                             {"paused", ps.paused},
                             {"position", ps.position},
                             {"duration", ps.duration},
                             {"volume", ps.volume},
                             {"subsVisible", ps.subsVisible},
                             {"buffered", e.getMessage()},
                             {"tracks", tracks}}
                            .dump(),
                        "application/json");
    });

    server.Post("/api/player/command", [&e](const httplib::Request& req, httplib::Response& res) {
        json in;
        try {
            in = json::parse(req.body);
        } catch (const std::exception&) {
            res.set_content("{\"error\":\"bad request\"}", "application/json");
            return;
        }
        const std::string action = in.value("action", "");
        if (action == "pause") {
            player::togglePause();
        } else if (action == "seek") {
            player::seekRelative(in.value("value", 0.0));
        } else if (action == "seekTo") {
            player::seekAbsolute(in.value("value", 0.0));
        } else if (action == "audio") {
            player::setAudioTrack(in.value("value", 0));
        } else if (action == "sub") {
            player::setSubTrack(in.value("value", 0));
        } else if (action == "subsVisible") {
            player::setSubsVisible(in.value("value", true));
        } else if (action == "volume") {
            player::setVolume(in.value("value", 100));
        } else if (action == "stop") {
            e.stopRequested = true;
            player::stop();
        }
        res.set_content("{\"ok\":true}", "application/json");
    });

    server.Get("/api/status", [&e](const httplib::Request&, httplib::Response& res) {
        json reply{
            {"message", e.getMessage()},
            {"progress", e.progress.load()},
            {"done", e.done.load()},
            {"playing", e.playing.load()},
            {"videoActive", e.videoActive.load()},
        };
        res.set_content(reply.dump(), "application/json");
    });
}

bool startBackground(int port, const std::string& savePath) {
    Engine& e = engine();
    e.savePath = savePath;
    loadSettings();
    track::load();
    library::start();
    {
        const Settings cfg = currentSettings();
        if (!cfg.savePath.empty()) e.savePath = cfg.savePath;
        applySessionSettings(e, cfg);
    }

    // Leaked deliberately: the server must outlive this call and lives until
    // the process exits.
    auto* server = new httplib::Server();
    installRoutes(*server, e);

    std::thread([server, port] { server->listen("127.0.0.1", port); }).detach();

    // Wait until it is actually accepting, so the window never navigates to a
    // dead port and shows an error page on first paint.
    for (int i = 0; i < 100; ++i) {
        if (server->is_running()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

}  // namespace tsuzuki::ui
