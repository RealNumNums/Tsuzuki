#include "async.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace tsuzuki::async {
namespace {

Notify g_notify = nullptr;

// A generation counter per job type. A worker only publishes its result if the
// generation it started with is still current, so a slow first search cannot
// land on top of a fast second one.
struct Slot {
    std::mutex mutex;
    std::atomic<int> generation{0};
    std::atomic<bool> running{false};
    bool ready = false;
};

Slot g_searchSlot;
ui::SearchOutcome g_searchResult;

Slot g_openSlot;
ui::OpenOutcome g_openResult;

Slot g_discoverSlot;
ui::Discovery g_discoverResult;

Slot g_moreSlot;
ui::MorePage g_moreResult;

std::atomic<bool> g_alive{true};

}  // namespace

void init(Notify notify) { g_notify = notify; }

void shutdown() { g_alive = false; }

// ------------------------------------------------------------------ search

void search(const std::string& query, int resolution) {
    const int gen = ++g_searchSlot.generation;
    g_searchSlot.running = true;
    {
        std::lock_guard<std::mutex> lock(g_searchSlot.mutex);
        g_searchSlot.ready = false;
    }

    std::thread([query, resolution, gen] {
        ui::SearchOutcome out = ui::search(query, resolution);
        if (!g_alive) return;
        if (g_searchSlot.generation.load() != gen) return;  // superseded

        {
            std::lock_guard<std::mutex> lock(g_searchSlot.mutex);
            g_searchResult = std::move(out);
            g_searchSlot.ready = true;
        }
        g_searchSlot.running = false;
        if (g_notify) g_notify();
    }).detach();
}

bool searchRunning() { return g_searchSlot.running; }

bool takeSearch(ui::SearchOutcome& out) {
    std::lock_guard<std::mutex> lock(g_searchSlot.mutex);
    if (!g_searchSlot.ready) return false;
    out = std::move(g_searchResult);
    g_searchSlot.ready = false;
    return true;
}

// -------------------------------------------------------------------- open

void open(const std::string& magnet, int episode, int anilistId) {
    const int gen = ++g_openSlot.generation;
    g_openSlot.running = true;
    {
        std::lock_guard<std::mutex> lock(g_openSlot.mutex);
        g_openSlot.ready = false;
    }

    std::thread([magnet, episode, anilistId, gen] {
        ui::OpenOutcome out = ui::open(magnet, episode, anilistId);
        if (!g_alive) return;
        if (g_openSlot.generation.load() != gen) return;

        {
            std::lock_guard<std::mutex> lock(g_openSlot.mutex);
            g_openResult = std::move(out);
            g_openSlot.ready = true;
        }
        g_openSlot.running = false;
        if (g_notify) g_notify();
    }).detach();
}

bool openRunning() { return g_openSlot.running; }

bool takeOpen(ui::OpenOutcome& out) {
    std::lock_guard<std::mutex> lock(g_openSlot.mutex);
    if (!g_openSlot.ready) return false;
    out = std::move(g_openResult);
    g_openSlot.ready = false;
    return true;
}

// ---------------------------------------------------------------- discover

void discover(const std::string& genre) {
    const int gen = ++g_discoverSlot.generation;
    g_discoverSlot.running = true;
    {
        std::lock_guard<std::mutex> lock(g_discoverSlot.mutex);
        g_discoverSlot.ready = false;
    }

    std::thread([genre, gen] {
        ui::Discovery out = ui::discover(genre);
        if (!g_alive) return;
        if (g_discoverSlot.generation.load() != gen) return;  // genre changed

        {
            std::lock_guard<std::mutex> lock(g_discoverSlot.mutex);
            g_discoverResult = std::move(out);
            g_discoverSlot.ready = true;
        }
        g_discoverSlot.running = false;
        if (g_notify) g_notify();
    }).detach();
}

bool discoverRunning() { return g_discoverSlot.running; }

bool takeDiscover(ui::Discovery& out) {
    std::lock_guard<std::mutex> lock(g_discoverSlot.mutex);
    if (!g_discoverSlot.ready) return false;
    out = std::move(g_discoverResult);
    g_discoverSlot.ready = false;
    return true;
}

// -------------------------------------------------------------------- more

void more(const std::string& shelfKey, const std::string& genre, int page) {
    const int gen = ++g_moreSlot.generation;
    g_moreSlot.running = true;
    {
        std::lock_guard<std::mutex> lock(g_moreSlot.mutex);
        g_moreSlot.ready = false;
    }

    std::thread([shelfKey, genre, page, gen] {
        ui::MorePage out = ui::discoverMore(shelfKey, genre, page);
        if (!g_alive) return;
        if (g_moreSlot.generation.load() != gen) return;

        {
            std::lock_guard<std::mutex> lock(g_moreSlot.mutex);
            g_moreResult = std::move(out);
            g_moreSlot.ready = true;
        }
        g_moreSlot.running = false;
        if (g_notify) g_notify();
    }).detach();
}

bool moreRunning() { return g_moreSlot.running; }

bool takeMore(ui::MorePage& out) {
    std::lock_guard<std::mutex> lock(g_moreSlot.mutex);
    if (!g_moreSlot.ready) return false;
    out = std::move(g_moreResult);
    g_moreSlot.ready = false;
    return true;
}

}  // namespace tsuzuki::async
