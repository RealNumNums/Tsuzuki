#pragma once

// The mascot, drawn in the corner of the window.
//
// She lives inside the application rather than in a window of her own: cut out
// of her background, so what shows is the character over whatever the app is
// already painting, with no panel or frame around her.

#include "gfx.hpp"

namespace tsuzuki::mascot {

// The artwork, decoded against this canvas and cached. Null until it is ready,
// and re-decoded by itself if the graphics device is replaced.
ID2D1Bitmap* art(gfx::Canvas& canvas);

// Drops the cached bitmap. Called when the device goes away.
void dropDecoded();

}  // namespace tsuzuki::mascot
