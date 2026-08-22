#pragma once

// Network work, kept off the interface thread.
//
// Searching several trackers and opening a torrent both take seconds. Doing
// either inline would freeze the window mid-frame, so each runs on a worker
// and the window is asked to repaint when the answer lands. Screens ask
// "running?" and "ready?" and are never blocked by either.

#include "../ui.hpp"

#include <string>
#include <vector>

namespace tsuzuki::async {

// Called from a worker when a job finishes, so the window can repaint. Must
// only post a message - it is not on the interface thread.
using Notify = void (*)();
void init(Notify);

// Starting a job while one is running replaces it: the older answer is
// discarded when it arrives, so a fast second search cannot be overtaken by a
// slow first one.
void search(const std::string& query, int resolution);
bool searchRunning();
bool takeSearch(ui::SearchOutcome& out);

void open(const std::string& magnet, int episode, int anilistId);
bool openRunning();
bool takeOpen(ui::OpenOutcome& out);

void discover(const std::string& genre);
bool discoverRunning();
bool takeDiscover(ui::Discovery& out);

void more(const std::string& shelfKey, const std::string& genre, int page);
bool moreRunning();
bool takeMore(ui::MorePage& out);

void schedule(long long fromUnix, long long toUnix, bool mineOnly);
bool scheduleRunning();
// Appends whatever pages have arrived since the last call, rather than
// replacing — the schedule comes in over a dozen round trips and the calendar
// draws what it has as it goes.
bool takeSchedule(std::vector<ui::AiringEntry>& out);

// The show the home banner leads with. One request, and the answer is held by
// the caller, so this runs once per featured show rather than once per frame.
void spotlight(int anilistId);
bool spotlightRunning();
bool takeSpotlight(ui::Spotlight& out);

void shutdown();

}  // namespace tsuzuki::async
