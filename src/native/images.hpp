#pragma once

// Cover art cache.
//
// Screens ask for a URL and get back either a bitmap or nullptr. A miss starts
// a download on a worker thread and the window is asked to redraw once it
// lands, so drawing never blocks on the network - a card with no art yet just
// shows its placeholder for a frame or two.
//
// Bytes are also kept on disk, so the second launch paints instantly and an
// offline start still has artwork.

#include "gfx.hpp"

#include <string>

namespace tsuzuki::images {

// Called on the UI thread whenever a fetch completes and something needs
// repainting.
using Invalidate = void (*)();

void start(gfx::Canvas* canvas, Invalidate invalidate);
void stop();

// Null until the image is ready. Never blocks.
ID2D1Bitmap* get(const std::string& url);

// Drop decoded bitmaps but keep the disk cache - used when the render target
// is rebuilt, since bitmaps belong to the device that made them.
void dropDecoded();

}  // namespace tsuzuki::images
