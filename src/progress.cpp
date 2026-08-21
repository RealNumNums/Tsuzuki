#include "progress.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace tsuzuki::progress {
namespace {

using nlohmann::json;

std::mutex g_mutex;
json g_store = json::object();
bool g_loaded = false;

std::string storePath() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki" : std::string(".");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "\\progress.json";
}

void save() {
    std::ofstream out(storePath(), std::ios::binary | std::ios::trunc);
    if (out) out << g_store.dump(2) << "\n";
}

}  // namespace

std::string keyFor(int anilistId, int episode, const std::string& infoHash, int fileIndex) {
    if (anilistId > 0 && episode > 0) {
        return "al:" + std::to_string(anilistId) + ":" + std::to_string(episode);
    }
    return "t:" + infoHash + ":" + std::to_string(fileIndex);
}

void load() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_loaded) return;
    g_loaded = true;

    std::ifstream in(storePath());
    if (!in) return;
    try {
        json j;
        in >> j;
        if (j.is_object()) g_store = std::move(j);
    } catch (const std::exception&) {
        // A corrupt file should cost the resume points, not the launch.
    }
}

Position get(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    Position p;
    if (!g_store.is_object() || !g_store.contains(key)) return p;

    const json& e = g_store[key];
    if (!e.is_object()) return p;

    p.known = true;
    if (e.contains("episode") && e["episode"].is_number_integer()) p.episode = e["episode"];
    if (e.contains("seconds") && e["seconds"].is_number()) p.seconds = e["seconds"];
    if (e.contains("duration") && e["duration"].is_number()) p.duration = e["duration"];
    if (e.contains("title") && e["title"].is_string()) p.title = e["title"];
    if (e.contains("updatedAt") && e["updatedAt"].is_number()) {
        p.updatedAt = e["updatedAt"].get<long long>();
    }
    return p;
}

void set(const std::string& key, const Position& p) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Losing a resume point is a nuisance; taking the app down with it is not.
    // A key or title that will not serialise must not escape this function.
    try {
        g_store[key] = {{"episode", p.episode},
                        {"seconds", p.seconds},
                        {"duration", p.duration},
                        {"title", p.title},
                        {"updatedAt", static_cast<long long>(std::time(nullptr))}};
        save();
    } catch (const std::exception&) {
        g_store.erase(key);
    }
}

void clear(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store.contains(key)) {
        g_store.erase(key);
        save();
    }
}

bool worthResuming(const Position& p) {
    if (!p.known || p.duration <= 0) return false;
    if (p.seconds < 30) return false;                 // barely started
    if (p.seconds > p.duration * 0.92) return false;  // effectively finished
    return true;
}

}  // namespace tsuzuki::progress
