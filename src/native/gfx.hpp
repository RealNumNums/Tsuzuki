#pragma once

// Direct2D drawing layer.
//
// Everything the interface draws goes through here: rounded cards, cover art,
// gradients and text. It exists so the screens can be written in terms of
// rectangles and colours instead of COM lifetimes, and so there is exactly one
// place that knows about device loss and DPI.
//
// Direct2D rather than Win32 controls because this is a media interface -
// cover art, gradient scrims, rounded cards, hover animation. Owner-drawing
// all of that on top of common controls is more work than drawing it directly,
// and DirectWrite gives proper text layout with real ellipsis trimming.

#include <d2d1_1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <windows.h>

#include <string>
#include <vector>

// Forward declared at global scope: writing it inside the namespace below
// would declare a different, unrelated type.
struct IDXGISwapChain1;

namespace tsuzuki::gfx {

struct Color {
    float r = 0, g = 0, b = 0, a = 1;
    constexpr Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
};

// 0xRRGGBB, as the stylesheet wrote them.
constexpr Color rgb(unsigned hex, float a = 1.0f) {
    return {static_cast<float>((hex >> 16) & 0xFF) / 255.0f,
            static_cast<float>((hex >> 8) & 0xFF) / 255.0f,
            static_cast<float>(hex & 0xFF) / 255.0f, a};
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w / 2; }
    float cy() const { return y + h / 2; }
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect inset(float d) const { return {x + d, y + d, w - d * 2, h - d * 2}; }
    Rect offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
};

// The live palette.
//
// Deliberately variables rather than constants: every screen reads these by
// name, so switching a theme is one write per token and no screen needs to
// know that themes exist at all. Only ever written from useTheme(), on the
// interface thread, between frames.
namespace theme {
inline Color bg = rgb(0x08080A);
inline Color panel = rgb(0x0D0D0F);  // header and rail chrome
inline Color card = rgb(0x121214);
inline Color cardHover = rgb(0x1B1B1E);
inline Color line = rgb(0x2A2A2E);
inline Color fg = rgb(0xF2F2F4);
inline Color dim = rgb(0x8C8C93);
inline Color accent = rgb(0xE8E8EC);
inline Color accentSoft = rgb(0xFFFFFF);
inline Color good = rgb(0xC8C8CE);
inline Color warn = rgb(0xA8A8B0);
inline Color bad = rgb(0xD46A6A);
inline Color shade = rgb(0x000000);
}  // namespace theme

// A complete named palette. The interface never holds one of these; it reads
// the live tokens above, which useTheme() fills in from the chosen palette.
struct Palette {
    const char* key;
    const wchar_t* name;
    Color bg, panel, card, cardHover, line, fg, dim, accent, accentSoft, good, warn, bad,
        shade;
    // Whether text on this background is dark - a light theme needs the
    // opposite ink on its accent-filled buttons.
    bool light = false;
};

// Everything the selector offers, the shipped default first.
const std::vector<Palette>& palettes();

// Repaints the tokens. An unrecognised key falls back to the default rather
// than leaving the interface half-themed.
void useTheme(const std::string& key);
const Palette& currentPalette();

// The ink to put on an accent-filled button, which differs per theme.
Color onAccent();

enum class Weight { Regular, Medium, Semibold, Bold };
enum class Align { Left, Center, Right };

// One text style, resolved to a DirectWrite format on demand.
struct Font {
    float size = 14;
    Weight weight = Weight::Regular;
    Align align = Align::Left;
    bool ellipsis = true;   // trim with "..." instead of overflowing
    bool wrap = false;      // multi-line
    float lineHeight = 0;   // 0 = font default
    // Icons come from the system icon font rather than from bitmaps, so they
    // stay sharp at any scaling and cost nothing to ship.
    bool icon = false;
};

// Glyphs in Segoe Fluent Icons, which ships with Windows 11 (and falls back to
// Segoe MDL2 Assets, which ships with 10). Named so the call sites read as
// something other than magic numbers.
namespace icons {
inline constexpr wchar_t home[] = L"";
inline constexpr wchar_t compass[] = L"";
inline constexpr wchar_t calendar[] = L"";
inline constexpr wchar_t settings[] = L"";
}  // namespace icons

// Device-independent startup. Call once.
bool init();
void shutdown();

// Per-window rendering surface.
class Canvas {
  public:
    bool attach(HWND hwnd);
    void detach();
    void resize(unsigned w, unsigned h);

    // Returns false if the device was lost and the frame should be skipped;
    // resources are rebuilt before the next begin().
    bool begin(Color clear);
    void end();

    float dpiScale() const { return scale_; }
    // Shrinks by the current origin, so a screen laying out against bounds()
    // fills the space it was actually given rather than the whole window.
    Rect bounds() const { return {0, 0, width_ - originX_, height_ - originY_}; }

    // Draws everything after this shifted right and down, so a screen can be
    // written as though the window started here. Without it, giving the window
    // a navigation rail would mean an inset added by hand in every screen.
    void pushOrigin(float x, float y);
    void popOrigin();

    void fill(const Rect&, Color, float radius = 0);
    void stroke(const Rect&, Color, float radius = 0, float width = 1);
    // Vertical gradient - used for the scrim under cover art so white text
    // stays readable whatever the image behind it is doing.
    void gradient(const Rect&, Color top, Color bottom, float radius = 0);
    // Left to right. The banner needs one: shading the whole image top-down
    // to make the text readable buries the artwork, whereas shading only the
    // side the text is on leaves the picture visible.
    void gradientH(const Rect&, Color leftColor, Color rightColor, float radius = 0);

    void text(const std::wstring&, const Rect&, Color, const Font&);
    // Height the string needs at this width, for laying out before drawing.
    float measure(const std::wstring&, float maxWidth, const Font&);

    // Draws cover art cropped to fill the rect, centred, without distorting it.
    void image(ID2D1Bitmap* bmp, const Rect&, float radius = 0, float alpha = 1.0f);

    void pushClip(const Rect&);
    void popClip();

    ID2D1DeviceContext* context() const { return ctx_; }

  private:
    bool createTarget();
    void releaseTarget();
    IDWriteTextFormat* format(const Font&);

    HWND hwnd_ = nullptr;
    ID2D1DeviceContext* ctx_ = nullptr;
    ::IDXGISwapChain1* swap_ = nullptr;
    ID2D1Bitmap1* backBuffer_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    float width_ = 0, height_ = 0;
    float originX_ = 0, originY_ = 0;
    float scale_ = 1.0f;
    int clipDepth_ = 0;
};

// Decodes an image already in memory (a downloaded cover) into a bitmap owned
// by the caller. Returns nullptr if the bytes are not an image.
ID2D1Bitmap* decode(Canvas&, const void* data, size_t size);

IWICImagingFactory* wic();

}  // namespace tsuzuki::gfx
