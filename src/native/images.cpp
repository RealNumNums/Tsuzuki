#include "images.hpp"

#include "../http.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace tsuzuki::images {
namespace {

struct Entry {
    ID2D1Bitmap* bitmap = nullptr;
    std::vector<char> bytes;  // kept so the bitmap can be rebuilt after device loss
    bool failed = false;
};

std::mutex g_mutex;
std::map<std::string, Entry> g_cache;
std::set<std::string> g_inFlight;
std::deque<std::string> g_queue;
std::condition_variable g_wake;

gfx::Canvas* g_canvas = nullptr;
Invalidate g_invalidate = nullptr;
std::atomic<bool> g_running{false};
std::vector<std::thread> g_workers;

// Cheap, stable filename for a URL. Not a real hash - it only has to avoid
// collisions between a few hundred cover URLs.
std::string cacheName(const std::string& url) {
    unsigned long long h = 1469598103934665603ULL;
    for (const unsigned char c : url) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%016llx", h);
    return std::string(buf) + ".img";
}

std::string cacheDir() {
    const char* base = std::getenv("LOCALAPPDATA");
    std::string dir = base ? std::string(base) + "\\Tsuzuki\\covers" : std::string("covers");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

bool readFile(const std::string& path, std::vector<char>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size <= 0) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    return static_cast<bool>(f.read(out.data(), size));
}

void writeFile(const std::string& path, const std::vector<char>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (f) f.write(data.data(), static_cast<std::streamsize>(data.size()));
}

void worker() {
    while (g_running) {
        std::string url;
        {
            std::unique_lock<std::mutex> lock(g_mutex);
            g_wake.wait(lock, [] { return !g_running || !g_queue.empty(); });
            if (!g_running) return;
            url = g_queue.front();
            g_queue.pop_front();
        }

        const std::string path = cacheDir() + "\\" + cacheName(url);
        std::vector<char> bytes;

        if (!readFile(path, bytes)) {
            const http::Response res = http::get(url, 15);
            if (res.ok && !res.body.empty()) {
                bytes.assign(res.body.begin(), res.body.end());
                writeFile(path, bytes);
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_inFlight.erase(url);
            Entry& e = g_cache[url];
            if (bytes.empty()) {
                e.failed = true;
            } else {
                e.bytes = std::move(bytes);
            }
        }

        // Decoding needs the render target, which belongs to the UI thread, so
        // the bytes are handed over and get() does the decode on first use.
        if (g_invalidate) g_invalidate();
    }
}

}  // namespace

void start(gfx::Canvas* canvas, Invalidate invalidate) {
    g_canvas = canvas;
    g_invalidate = invalidate;
    if (g_running.exchange(true)) return;
    // Three at a time: enough to fill a screen of covers quickly without
    // opening a dozen sockets to the same CDN.
    for (int i = 0; i < 3; ++i) g_workers.emplace_back(worker);
}

void stop() {
    if (!g_running.exchange(false)) return;
    g_wake.notify_all();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_workers.clear();

    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& [url, e] : g_cache) {
        if (e.bitmap) e.bitmap->Release();
    }
    g_cache.clear();
}

ID2D1Bitmap* get(const std::string& url) {
    if (url.empty() || !g_canvas) return nullptr;

    std::vector<char> toDecode;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto it = g_cache.find(url);
        if (it != g_cache.end()) {
            if (it->second.bitmap) return it->second.bitmap;
            if (it->second.failed) return nullptr;
            if (!it->second.bytes.empty()) toDecode = it->second.bytes;
        } else if (g_inFlight.insert(url).second) {
            g_queue.push_back(url);
            g_wake.notify_one();
        }
    }

    if (toDecode.empty()) return nullptr;

    ID2D1Bitmap* bmp = gfx::decode(*g_canvas, toDecode.data(), toDecode.size());
    std::lock_guard<std::mutex> lock(g_mutex);
    Entry& e = g_cache[url];
    if (!bmp) {
        e.failed = true;
        return nullptr;
    }
    if (e.bitmap) e.bitmap->Release();  // lost a race; keep the newer one
    e.bitmap = bmp;
    return bmp;
}

void dropDecoded() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (auto& [url, e] : g_cache) {
        if (e.bitmap) {
            e.bitmap->Release();
            e.bitmap = nullptr;
        }
    }
}

}  // namespace tsuzuki::images
