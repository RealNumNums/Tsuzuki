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

// The palette the page used, kept so the native interface looks like the same
// product rather than a different one.
namespace theme {
inline constexpr Color bg = rgb(0x0B0B10);
inline constexpr Color card = rgb(0x14141C);
inline constexpr Color cardHover = rgb(0x1A1A24);
inline constexpr Color line = rgb(0x262633);
inline constexpr Color fg = rgb(0xECECF2);
inline constexpr Color dim = rgb(0x8A8A99);
inline constexpr Color accent = rgb(0xFF5C8A);
inline constexpr Color accentSoft = rgb(0xFF9DBB);
inline constexpr Color good = rgb(0x4AC97E);
inline constexpr Color warn = rgb(0xE0B341);
inline constexpr Color bad = rgb(0xE05F5F);
inline constexpr Color shade = rgb(0x000000);
}  // namespace theme

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
};

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
    Rect bounds() const { return {0, 0, width_, height_}; }

    void fill(const Rect&, Color, float radius = 0);
    void stroke(const Rect&, Color, float radius = 0, float width = 1);
    // Vertical gradient - used for the scrim under cover art so white text
    // stays readable whatever the image behind it is doing.
    void gradient(const Rect&, Color top, Color bottom, float radius = 0);

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
    float scale_ = 1.0f;
    int clipDepth_ = 0;
};

// Decodes an image already in memory (a downloaded cover) into a bitmap owned
// by the caller. Returns nullptr if the bytes are not an image.
ID2D1Bitmap* decode(Canvas&, const void* data, size_t size);

IWICImagingFactory* wic();

}  // namespace tsuzuki::gfx
