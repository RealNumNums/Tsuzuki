// The mascot artwork. See mascot.hpp; the drawing itself is in view.cpp,
// because she is just another thing the frame paints.

#include "mascot.hpp"

namespace tsuzuki::mascot {
namespace {

constexpr int kResourceId = 102;  // the PNG, embedded rather than shipped loose

ID2D1Bitmap* g_bitmap = nullptr;
ID2D1DeviceContext* g_decodedOn = nullptr;

}  // namespace

ID2D1Bitmap* art(gfx::Canvas& canvas) {
    // A bitmap belongs to the device that made it, and the canvas rebuilds its
    // device silently after a loss, so notice when the context has changed
    // underneath us rather than drawing with a dead one.
    if (g_bitmap && canvas.context() == g_decodedOn) return g_bitmap;
    dropDecoded();

    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC found = FindResourceW(self, MAKEINTRESOURCEW(kResourceId),
                                reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (!found) return nullptr;
    HGLOBAL loaded = LoadResource(self, found);
    if (!loaded) return nullptr;

    const void* data = LockResource(loaded);
    const DWORD size = SizeofResource(self, found);
    if (!data || !size) return nullptr;

    g_bitmap = gfx::decode(canvas, data, size);
    g_decodedOn = canvas.context();
    return g_bitmap;
}

void dropDecoded() {
    if (g_bitmap) {
        g_bitmap->Release();
        g_bitmap = nullptr;
    }
    g_decodedOn = nullptr;
}

}  // namespace tsuzuki::mascot
