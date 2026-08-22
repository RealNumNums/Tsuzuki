#include "gfx.hpp"

#include <d3d11.h>
#include <dxgi1_2.h>

#include <map>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace tsuzuki::gfx {
namespace {

ID2D1Factory1* g_factory = nullptr;
IDWriteFactory* g_write = nullptr;
IWICImagingFactory* g_wic = nullptr;
ID3D11Device* g_d3d = nullptr;
ID2D1Device* g_device = nullptr;

template <class T>
void release(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

D2D1_COLOR_F toD2D(Color c) { return D2D1::ColorF(c.r, c.g, c.b, c.a); }
D2D1_RECT_F toD2D(const Rect& r) { return D2D1::RectF(r.x, r.y, r.right(), r.bottom()); }

DWRITE_FONT_WEIGHT weightOf(Weight w) {
    switch (w) {
        case Weight::Medium: return DWRITE_FONT_WEIGHT_MEDIUM;
        case Weight::Semibold: return DWRITE_FONT_WEIGHT_SEMI_BOLD;
        case Weight::Bold: return DWRITE_FONT_WEIGHT_BOLD;
        case Weight::Regular: break;
    }
    return DWRITE_FONT_WEIGHT_NORMAL;
}

// Text formats are immutable and shared, so they are cached rather than made
// per draw call - a home screen is several hundred of them a frame otherwise.
struct FormatKey {
    float size;
    int weight;
    int align;
    bool wrap;
    float lineHeight;
    bool icon;
    bool operator<(const FormatKey& o) const {
        if (size != o.size) return size < o.size;
        if (weight != o.weight) return weight < o.weight;
        if (align != o.align) return align < o.align;
        if (wrap != o.wrap) return wrap < o.wrap;
        if (lineHeight != o.lineHeight) return lineHeight < o.lineHeight;
        return icon < o.icon;
    }
};
std::map<FormatKey, IDWriteTextFormat*> g_formats;

}  // namespace

IWICImagingFactory* wic() { return g_wic; }

bool init() {
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                 reinterpret_cast<void**>(&g_factory)))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&g_write)))) {
        return false;
    }
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&g_wic)))) {
        return false;
    }

    // Hardware first, WARP if there is no usable GPU - a remote session or a
    // stripped VM should still draw rather than refusing to start.
    const D3D_DRIVER_TYPE types[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    for (const D3D_DRIVER_TYPE type : types) {
        const HRESULT hr = D3D11CreateDevice(
            nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &g_d3d, nullptr, nullptr);
        if (SUCCEEDED(hr)) break;
    }
    if (!g_d3d) return false;

    IDXGIDevice* dxgi = nullptr;
    if (FAILED(g_d3d->QueryInterface(IID_PPV_ARGS(&dxgi)))) return false;
    const HRESULT hr = g_factory->CreateDevice(dxgi, &g_device);
    release(dxgi);
    return SUCCEEDED(hr);
}

void shutdown() {
    for (auto& [key, fmt] : g_formats) release(fmt);
    g_formats.clear();
    release(g_device);
    release(g_d3d);
    release(g_wic);
    release(g_write);
    release(g_factory);
}

bool Canvas::attach(HWND hwnd) {
    hwnd_ = hwnd;
    scale_ = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
    if (scale_ <= 0) scale_ = 1.0f;
    return createTarget();
}

void Canvas::detach() {
    releaseTarget();
    hwnd_ = nullptr;
}

bool Canvas::createTarget() {
    if (!g_device || !hwnd_) return false;

    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT pxW = static_cast<UINT>(rc.right - rc.left);
    const UINT pxH = static_cast<UINT>(rc.bottom - rc.top);
    if (pxW == 0 || pxH == 0) return false;

    if (FAILED(g_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_))) return false;

    IDXGIDevice* dxgi = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    if (FAILED(g_d3d->QueryInterface(IID_PPV_ARGS(&dxgi)))) return false;
    dxgi->GetAdapter(&adapter);
    if (adapter) adapter->GetParent(IID_PPV_ARGS(&factory));

    bool ok = false;
    if (factory) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = pxW;
        desc.Height = pxH;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        ok = SUCCEEDED(factory->CreateSwapChainForHwnd(g_d3d, hwnd_, &desc, nullptr, nullptr, &swap_));
    }
    release(factory);
    release(adapter);
    release(dxgi);
    if (!ok) return false;

    IDXGISurface* surface = nullptr;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    const auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    const HRESULT hr = ctx_->CreateBitmapFromDxgiSurface(surface, &props, &backBuffer_);
    release(surface);
    if (FAILED(hr)) return false;

    ctx_->SetTarget(backBuffer_);
    // Everything is laid out in DIPs; the device context scales to pixels, so
    // the screens never do DPI arithmetic themselves.
    ctx_->SetDpi(scale_ * 96.0f, scale_ * 96.0f);
    ctx_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &brush_);

    width_ = static_cast<float>(pxW) / scale_;
    height_ = static_cast<float>(pxH) / scale_;
    return true;
}

void Canvas::releaseTarget() {
    release(brush_);
    release(backBuffer_);
    release(swap_);
    release(ctx_);
}

void Canvas::resize(unsigned, unsigned) {
    if (!swap_ || !ctx_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const UINT pxW = static_cast<UINT>(rc.right - rc.left);
    const UINT pxH = static_cast<UINT>(rc.bottom - rc.top);
    if (pxW == 0 || pxH == 0) return;

    scale_ = static_cast<float>(GetDpiForWindow(hwnd_)) / 96.0f;
    if (scale_ <= 0) scale_ = 1.0f;

    ctx_->SetTarget(nullptr);
    release(backBuffer_);
    if (FAILED(swap_->ResizeBuffers(0, pxW, pxH, DXGI_FORMAT_UNKNOWN, 0))) return;

    IDXGISurface* surface = nullptr;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return;
    const auto props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
    ctx_->CreateBitmapFromDxgiSurface(surface, &props, &backBuffer_);
    release(surface);

    ctx_->SetTarget(backBuffer_);
    ctx_->SetDpi(scale_ * 96.0f, scale_ * 96.0f);
    width_ = static_cast<float>(pxW) / scale_;
    height_ = static_cast<float>(pxH) / scale_;
}

void Canvas::pushOrigin(float x, float y) {
    originX_ += x;
    originY_ += y;
    if (ctx_) ctx_->SetTransform(D2D1::Matrix3x2F::Translation(originX_, originY_));
}

void Canvas::popOrigin() {
    originX_ = 0;
    originY_ = 0;
    if (ctx_) ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
}

bool Canvas::begin(Color clear) {
    if (!ctx_ && !createTarget()) return false;
    clipDepth_ = 0;
    originX_ = originY_ = 0;
    ctx_->SetTransform(D2D1::Matrix3x2F::Identity());
    ctx_->BeginDraw();
    ctx_->Clear(toD2D(clear));
    return true;
}

void Canvas::end() {
    if (!ctx_) return;
    while (clipDepth_ > 0) popClip();
    const HRESULT hr = ctx_->EndDraw();
    if (SUCCEEDED(hr) && swap_) swap_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET) {
        // The GPU went away - a driver update, or waking from sleep. Drop
        // everything and rebuild on the next frame rather than dying.
        releaseTarget();
    }
}

void Canvas::fill(const Rect& r, Color c, float radius) {
    if (!brush_) return;
    brush_->SetColor(toD2D(c));
    if (radius > 0) {
        ctx_->FillRoundedRectangle(D2D1::RoundedRect(toD2D(r), radius, radius), brush_);
    } else {
        ctx_->FillRectangle(toD2D(r), brush_);
    }
}

void Canvas::stroke(const Rect& r, Color c, float radius, float width) {
    if (!brush_) return;
    brush_->SetColor(toD2D(c));
    // Inset by half the stroke so the line lands inside the rect, which is
    // what every layout here assumes.
    const Rect in = r.inset(width / 2);
    if (radius > 0) {
        ctx_->DrawRoundedRectangle(D2D1::RoundedRect(toD2D(in), radius, radius), brush_, width);
    } else {
        ctx_->DrawRectangle(toD2D(in), brush_, width);
    }
}

void Canvas::gradient(const Rect& r, Color top, Color bottom, float radius) {
    if (!ctx_) return;
    D2D1_GRADIENT_STOP stops[2] = {{0.0f, toD2D(top)}, {1.0f, toD2D(bottom)}};
    ID2D1GradientStopCollection* collection = nullptr;
    if (FAILED(ctx_->CreateGradientStopCollection(stops, 2, &collection))) return;

    ID2D1LinearGradientBrush* g = nullptr;
    ctx_->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.x, r.y), D2D1::Point2F(r.x, r.bottom())),
        collection, &g);
    if (g) {
        if (radius > 0) {
            ctx_->FillRoundedRectangle(D2D1::RoundedRect(toD2D(r), radius, radius), g);
        } else {
            ctx_->FillRectangle(toD2D(r), g);
        }
        release(g);
    }
    release(collection);
}

IDWriteTextFormat* Canvas::format(const Font& f) {
    const FormatKey key{f.size,       static_cast<int>(f.weight), static_cast<int>(f.align),
                        f.wrap,        f.lineHeight,               f.icon};
    const auto it = g_formats.find(key);
    if (it != g_formats.end()) return it->second;

    IDWriteTextFormat* fmt = nullptr;
    // Windows 11 has the Fluent set; on 10 the name misses and DirectWrite
    // falls back, so ask for MDL2 explicitly rather than getting Segoe UI.
    const wchar_t* family = f.icon ? L"Segoe Fluent Icons" : L"Segoe UI";
    if (FAILED(g_write->CreateTextFormat(family, nullptr, weightOf(f.weight),
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         f.size, L"en-us", &fmt))) {
        if (!f.icon) return nullptr;
        if (FAILED(g_write->CreateTextFormat(L"Segoe MDL2 Assets", nullptr, weightOf(f.weight),
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             f.size, L"en-us", &fmt))) {
            return nullptr;
        }
    }
    fmt->SetTextAlignment(f.align == Align::Center   ? DWRITE_TEXT_ALIGNMENT_CENTER
                          : f.align == Align::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                    : DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetWordWrapping(f.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (f.lineHeight > 0) {
        fmt->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, f.lineHeight, f.lineHeight * 0.8f);
    }
    g_formats[key] = fmt;
    return fmt;
}

void Canvas::text(const std::wstring& s, const Rect& r, Color c, const Font& f) {
    if (!brush_ || s.empty()) return;
    IDWriteTextFormat* fmt = format(f);
    if (!fmt) return;

    IDWriteTextLayout* layout = nullptr;
    if (FAILED(g_write->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, r.w, r.h,
                                         &layout))) {
        return;
    }
    if (f.ellipsis) {
        IDWriteInlineObject* sign = nullptr;
        if (SUCCEEDED(g_write->CreateEllipsisTrimmingSign(fmt, &sign))) {
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            layout->SetTrimming(&trim, sign);
            release(sign);
        }
    }
    brush_->SetColor(toD2D(c));
    ctx_->DrawTextLayout(D2D1::Point2F(r.x, r.y), layout, brush_,
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    release(layout);
}

float Canvas::measure(const std::wstring& s, float maxWidth, const Font& f) {
    if (s.empty()) return 0;
    IDWriteTextFormat* fmt = format(f);
    if (!fmt) return 0;
    IDWriteTextLayout* layout = nullptr;
    if (FAILED(g_write->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, maxWidth,
                                         4000.0f, &layout))) {
        return 0;
    }
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    release(layout);
    return m.height;
}

void Canvas::image(ID2D1Bitmap* bmp, const Rect& r, float radius, float alpha) {
    if (!bmp || !ctx_) return;

    const D2D1_SIZE_F size = bmp->GetSize();
    if (size.width <= 0 || size.height <= 0) return;

    // Cover-fit: scale so the image fills the rect, then centre the overflow.
    const float scale = (r.w / size.width > r.h / size.height) ? r.w / size.width
                                                               : r.h / size.height;
    const float dw = size.width * scale, dh = size.height * scale;
    const Rect dst{r.cx() - dw / 2, r.cy() - dh / 2, dw, dh};

    const bool clipped = radius > 0;
    ID2D1RoundedRectangleGeometry* geo = nullptr;
    ID2D1Layer* layer = nullptr;
    if (clipped) {
        g_factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(toD2D(r), radius, radius), &geo);
        if (geo) {
            ctx_->CreateLayer(nullptr, &layer);
            if (layer) {
                ctx_->PushLayer(D2D1::LayerParameters(toD2D(r), geo), layer);
            }
        }
    } else {
        ctx_->PushAxisAlignedClip(toD2D(r), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }

    ctx_->DrawBitmap(bmp, toD2D(dst), alpha, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);

    if (clipped) {
        if (layer) ctx_->PopLayer();
        release(layer);
        release(geo);
    } else {
        ctx_->PopAxisAlignedClip();
    }
}

void Canvas::pushClip(const Rect& r) {
    if (!ctx_) return;
    ctx_->PushAxisAlignedClip(toD2D(r), D2D1_ANTIALIAS_MODE_ALIASED);
    ++clipDepth_;
}

void Canvas::popClip() {
    if (!ctx_ || clipDepth_ <= 0) return;
    ctx_->PopAxisAlignedClip();
    --clipDepth_;
}

ID2D1Bitmap* decode(Canvas& canvas, const void* data, size_t size) {
    if (!g_wic || !canvas.context() || !data || size == 0) return nullptr;

    IWICStream* stream = nullptr;
    if (FAILED(g_wic->CreateStream(&stream))) return nullptr;
    if (FAILED(stream->InitializeFromMemory(
            reinterpret_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size)))) {
        release(stream);
        return nullptr;
    }

    IWICBitmapDecoder* decoder = nullptr;
    if (FAILED(g_wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                              &decoder))) {
        release(stream);
        return nullptr;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    ID2D1Bitmap* out = nullptr;
    if (SUCCEEDED(decoder->GetFrame(0, &frame)) &&
        SUCCEEDED(g_wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone, nullptr, 0.0,
                                        WICBitmapPaletteTypeMedianCut))) {
        canvas.context()->CreateBitmapFromWicBitmap(converter, nullptr, &out);
    }
    release(converter);
    release(frame);
    release(decoder);
    release(stream);
    return out;
}

}  // namespace tsuzuki::gfx
