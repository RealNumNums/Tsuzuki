// Tsuzuki - native application entry point.
//
// A Win32 window drawing its own interface with Direct2D. The engine
// (libtorrent, Anitomy, the source layer, the watch database) runs in-process
// exactly as it does for the CLI, and the interface calls into it directly -
// there is no view layer in another language and nothing is serialised on the
// way to the screen.
//
// The one web view left is the AniList sign-in window, which exists to render
// AniList's own login page. That is a third-party website, not our interface,
// and hosting it is what lets the token be read straight out of the redirect.

#include <windows.h>

#include <dwmapi.h>
#include <mmsystem.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wrl.h>

#include <WebView2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "native/gfx.hpp"
#include "native/async.hpp"
#include "native/images.hpp"
#include "native/view.hpp"
#include "ui.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kPort = 7654;
constexpr wchar_t kClassName[] = L"TsuzukiAppWindow";

// Only the login window needs WebView2 now, but the environment has to exist
// before a controller can be made, so it is still created at startup.
ComPtr<ICoreWebView2Environment> g_env;

HWND g_authWnd = nullptr;
ComPtr<ICoreWebView2Controller> g_authController;
ComPtr<ICoreWebView2> g_authView;
constexpr UINT WM_START_AUTH = WM_APP + 2;
std::wstring g_authUrl;

// Child window mpv draws into. Hidden until something plays, so the interface
// owns the whole client area the rest of the time.
HWND g_video = nullptr;
HWND g_main = nullptr;
constexpr UINT WM_PLAYBACK = WM_APP + 1;
constexpr UINT WM_IMAGE_READY = WM_APP + 4;
constexpr UINT_PTR kAnimTimer = 1;
constexpr UINT_PTR kOverlayTimer = 2;

tsuzuki::gfx::Canvas g_canvas;
tsuzuki::view::State g_state;
tsuzuki::view::Input g_input;
// One Ui for the life of the window, not one per frame. It carries which
// element is being pressed and how far each hover has eased in - rebuilding it
// each frame reset both, so a press and its release landed on different
// objects and no click was ever recognised.
tsuzuki::view::Ui g_ui(g_canvas, g_input);

// The control bar floats above the picture in a child window of its own.
// mpv renders into a sibling child window, and a child window always
// composites over its parent - Direct2D drawn on the parent simply cannot
// appear on top of it. A second child, layered so it can fade, can.
HWND g_overlay = nullptr;
tsuzuki::gfx::Canvas g_overlayCanvas;
tsuzuki::view::Ui g_overlayUi(g_overlayCanvas, g_input);
// How far the bar is out: 0 fully tucked below the bottom edge, 1 fully up.
// It slides rather than fades because a layered child window refuses
// SetLayeredWindowAttributes here (error 87), and sliding reads just as well
// over video anyway.
float g_barShow = 0.0f;
DWORD g_lastActivity = 0;     // when the pointer last moved
POINT g_lastPointer{-1, -1};
DWORD g_lastTick = 0;        // for time-based easing
DWORD g_lastBarPaint = 0;
bool g_highResTimer = false;

constexpr int kBarH = 84;
constexpr int kBarMaxW = 940;
constexpr int kBarLift = 28;  // gap between the bar and the bottom edge
constexpr DWORD kIdleHideMs = 2600;
bool g_playing = false;
bool g_animating = false;

// Called from the engine worker thread, so it only posts.
void onPlaybackActive(bool active) {
    if (g_main) PostMessageW(g_main, WM_PLAYBACK, active ? 1 : 0, 0);
}

// Height of the control strip shown under the video while something plays.
constexpr int kControlBar = 92;

// The bar is a rounded pill centred over the bottom of the picture. The
// window region does the rounding, because a layered window with uniform
// alpha cannot round its own corners by drawing.
// Sizing is separate from moving, and deliberately so. Rebuilding the
// rounded region and resizing the swap chain are both expensive - the swap
// chain in particular releases its back buffer and rebuilds the render
// target - and doing either on every animation frame is what made the slide
// stutter. The bar never changes size mid-slide, only its Y, so this runs
// once per real size change and the animation only ever moves the window.
int g_barW = 0, g_barH = 0;

void sizeOverlay(HWND hwnd) {
    if (!g_overlay) return;
    RECT b{};
    GetClientRect(hwnd, &b);
    const int w = b.right - b.left;

    const float scale = g_canvas.dpiScale();
    const int barH = static_cast<int>(kBarH * scale);
    const int barW =
        (std::min)(static_cast<int>(kBarMaxW * scale), w - static_cast<int>(60 * scale));
    if (barW == g_barW && barH == g_barH) return;

    g_barW = barW;
    g_barH = barH;
    SetWindowPos(g_overlay, nullptr, 0, 0, barW, barH,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    HRGN rgn = CreateRoundRectRgn(0, 0, barW + 1, barH + 1,
                                  static_cast<int>(18 * scale), static_cast<int>(18 * scale));
    SetWindowRgn(g_overlay, rgn, TRUE);  // the window now owns the region
    g_overlayCanvas.resize(0, 0);
}

// Just the slide. A child window is clipped to its parent, so the part
// pushed below the bottom edge simply is not drawn - no masking needed.
void positionOverlay(HWND hwnd) {
    if (!g_overlay) return;
    RECT b{};
    GetClientRect(hwnd, &b);
    const int w = b.right - b.left;
    const int h = b.bottom - b.top;

    const float scale = g_canvas.dpiScale();
    const int x = (w - g_barW) / 2;
    const int restY = h - g_barH - static_cast<int>(kBarLift * scale);
    const int hiddenY = h + 4;
    const int y = static_cast<int>(hiddenY + (restY - hiddenY) * g_barShow + 0.5f);

    SetWindowPos(g_overlay, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void layoutOverlay(HWND hwnd) {
    sizeOverlay(hwnd);
    positionOverlay(hwnd);
}

void layoutVideo(HWND hwnd, bool playing) {
    RECT b{};
    GetClientRect(hwnd, &b);
    const int w = b.right - b.left;
    const int h = b.bottom - b.top;

    if (playing && g_video) {
        // The whole client area: nothing is reserved for controls any more,
        // so hiding them gives the picture the entire window.
        MoveWindow(g_video, 0, 0, w, h, TRUE);
        ShowWindow(g_video, SW_SHOW);
        sizeOverlay(hwnd);
        positionOverlay(hwnd);
    } else {
        if (g_video) ShowWindow(g_video, SW_HIDE);
        if (g_overlay) ShowWindow(g_overlay, SW_HIDE);
        g_barShow = 0.0f;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

// Fade towards shown or hidden. Shown while the pointer has moved recently,
// or whenever it is over the bar itself - grabbing the scrubber must not
// make the thing you are aiming at disappear.
void updateOverlay(HWND hwnd) {
    if (!g_overlay || !g_playing) return;

    POINT p{};
    GetCursorPos(&p);
    // One pixel is enough. Anything larger and a gentle nudge of the mouse -
    // exactly what someone does when they want the controls back - is ignored.
    if (std::abs(p.x - g_lastPointer.x) > 1 || std::abs(p.y - g_lastPointer.y) > 1) {
        g_lastPointer = p;
        g_lastActivity = GetTickCount();
    }

    RECT bar{};
    GetWindowRect(g_overlay, &bar);
    const bool overBar = p.x >= bar.left && p.x < bar.right && p.y >= bar.top && p.y < bar.bottom;
    const bool recent = GetTickCount() - g_lastActivity < kIdleHideMs;
    const float target = (overBar || recent) ? 1.0f : 0.0f;

    // Eased against elapsed time rather than a fixed step per tick, so an
    // irregular timer produces even motion instead of visible unevenness.
    const DWORD now = GetTickCount();
    float dt = (now - g_lastTick) / 1000.0f;
    g_lastTick = now;
    if (dt <= 0 || dt > 0.25f) dt = 0.016f;  // first tick, or a stall

    const float wasShow = g_barShow;
    const float k = 1.0f - std::exp(-dt / 0.075f);  // ~75ms time constant
    g_barShow += (target - g_barShow) * k;
    if (std::fabs(target - g_barShow) < 0.002f) g_barShow = target;

    const bool moving = std::fabs(g_barShow - wasShow) > 0.0005f;

    if (g_barShow <= 0.002f) {
        g_barShow = 0.0f;
        if (IsWindowVisible(g_overlay)) ShowWindow(g_overlay, SW_HIDE);
        return;
    }

    if (moving) positionOverlay(hwnd);
    if (!IsWindowVisible(g_overlay)) {
        // NOACTIVATE, or showing the bar would steal focus from the video.
        ShowWindow(g_overlay, SW_SHOWNOACTIVATE);
        SetWindowPos(g_overlay, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // The contents only change when the playhead does, so redraw four times
    // a second rather than on every frame of the slide - moving the window
    // already blits what is there.
    if (now - g_lastBarPaint > 240) {
        g_lastBarPaint = now;
        InvalidateRect(g_overlay, nullptr, FALSE);
    }
}

void renderOverlay() {
    using namespace tsuzuki;
    if (!g_overlayCanvas.begin(gfx::rgb(0x0E0E14))) return;
    g_overlayUi.beginFrame();
    view::playerStrip(g_overlayUi, g_state);
    g_overlayUi.endFrame();
    g_overlayCanvas.end();

    g_input.mousePressed = false;
    g_input.mouseReleased = false;
}

void render(HWND hwnd) {
    using namespace tsuzuki;

    bool scrolling = false;

    if (!g_canvas.begin(gfx::theme::bg)) return;

    // The picture covers the whole client area while it plays, and the
    // controls live in their own window, so there is nothing to draw here.
    if (g_playing) {
        g_canvas.end();
        return;
    }

    // While the video is up, the interface is only the strip underneath it -
    // the picture belongs to a child window and must not be painted over.
    const gfx::Rect full = g_canvas.bounds();

    const int idx = static_cast<int>(g_state.screen);

    // Ease towards the target before drawing, not after. Clamping a frame
    // that had already been painted is what made fast scrolling flash a
    // blank page and snap back on every notch.
    {
        float maxScroll = g_state.contentH[idx] - full.h;
        if (maxScroll < 0) maxScroll = 0;
        if (g_state.scrollTarget[idx] > maxScroll) g_state.scrollTarget[idx] = maxScroll;
        if (g_state.scrollTarget[idx] < 0) g_state.scrollTarget[idx] = 0;

        const float delta = g_state.scrollTarget[idx] - g_state.scroll[idx];
        if (std::fabs(delta) < 0.5f) {
            g_state.scroll[idx] = g_state.scrollTarget[idx];
        } else {
            g_state.scroll[idx] += delta * 0.25f;
            scrolling = true;
        }
    }

    g_ui.scrollY = g_state.scroll[idx];
    g_animating = view::frame(g_ui, g_state);

    // Height is only known once the screen has laid itself out, so it is
    // recorded for the next wheel event rather than used to correct this one.
    g_state.contentH[idx] = g_ui.contentHeight;

    g_canvas.end();

    // Input is edge-triggered: consumed once the frame that saw it is done.
    g_input.mousePressed = false;
    g_input.mouseReleased = false;
    g_input.wheel = 0;
    g_input.typed.clear();
    g_input.key = 0;

    if (g_animating || scrolling) {
        SetTimer(hwnd, kAnimTimer, 16, nullptr);
    } else {
        KillTimer(hwnd, kAnimTimer);
    }
}

std::wstring userDataFolder() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L".";
    base += L"\\Tsuzuki";
    CreateDirectoryW(base.c_str(), nullptr);
    return base;
}

std::string savePath() {
    char buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableA("TEMP", buf, MAX_PATH);
    const std::string base = (n > 0 && n < MAX_PATH) ? std::string(buf) : std::string(".");
    return base + "\\tsuzuki";
}

// Windows 10 1809+ honours this; older builds simply ignore it, which is why
// the result is not checked.
void useDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
}

// Mouse handling for the control bar. Coordinates arrive relative to the bar
// itself, which is exactly what playerStrip lays out against.
LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            renderOverlay();
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE: {
            const float scale = g_overlayCanvas.dpiScale();
            g_input.mouseX = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            g_input.mouseY = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
            g_lastActivity = GetTickCount();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            g_input.mouseDown = true;
            g_input.mousePressed = true;
            g_lastActivity = GetTickCount();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            g_input.mouseDown = false;
            g_input.mouseReleased = true;
            g_lastActivity = GetTickCount();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        // Never take focus: clicking a control must leave the keyboard with
        // the video, and the bar must not flash the title bar inactive.
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void logLine(const char* what, unsigned long code) {
    char path[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH)) return;
    strcat_s(path, "\\Tsuzuki\\startup.log");
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    fprintf(f, "%s: %lu\n", what, code);
    fclose(f);
}

void openAuthWindow(HINSTANCE instance, HWND owner);
void closeAuthWindow();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            g_canvas.resize(LOWORD(lp), HIWORD(lp));
            layoutVideo(hwnd, g_playing);
            if (g_playing) layoutOverlay(hwnd);
            return 0;

        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            g_canvas.resize(0, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            render(hwnd);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;  // the canvas clears; erasing here would flicker

        case WM_TIMER:
            if (wp == kAnimTimer) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (wp == kOverlayTimer) {
                updateOverlay(hwnd);
                return 0;
            }
            break;

        case WM_IMAGE_READY:
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_MOUSEMOVE: {
            const float scale = g_canvas.dpiScale();
            g_input.mouseX = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            g_input.mouseY = static_cast<float>(GET_Y_LPARAM(lp)) / scale;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSELEAVE:
            g_input.mouseX = g_input.mouseY = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN:
            SetCapture(hwnd);
            g_input.mouseDown = true;
            g_input.mousePressed = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_LBUTTONUP:
            ReleaseCapture();
            g_input.mouseDown = false;
            g_input.mouseReleased = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_MOUSEWHEEL: {
            const float notches = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / WHEEL_DELTA;
            const int idx = static_cast<int>(g_state.screen);
            RECT client{};
            GetClientRect(hwnd, &client);
            const float viewH =
                static_cast<float>(client.bottom - client.top) / g_canvas.dpiScale();

            // Only the target moves; the view eases towards it. Clamped here
            // as well as in render so holding the wheel at the bottom cannot
            // build up an offset that has to unwind afterwards.
            float maxScroll = g_state.contentH[idx] - viewH;
            if (maxScroll < 0) maxScroll = 0;
            g_state.scrollTarget[idx] -= notches * 110.0f;
            if (g_state.scrollTarget[idx] > maxScroll) g_state.scrollTarget[idx] = maxScroll;
            if (g_state.scrollTarget[idx] < 0) g_state.scrollTarget[idx] = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_CHAR:
            g_input.typed.push_back(static_cast<wchar_t>(wp));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_KEYDOWN: {
            g_input.key = static_cast<int>(wp);
            g_input.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

            // Only when nothing is being typed into, or Home would jump the
            // page while someone is editing a download folder.
            if (g_state.focusField == 0 && !g_state.queryFocused) {
                const int idx = static_cast<int>(g_state.screen);
                RECT client{};
                GetClientRect(hwnd, &client);
                const float viewH =
                    static_cast<float>(client.bottom - client.top) / g_canvas.dpiScale();
                if (wp == VK_NEXT) g_state.scrollTarget[idx] += viewH * 0.9f;
                if (wp == VK_PRIOR) g_state.scrollTarget[idx] -= viewH * 0.9f;
                if (wp == VK_HOME) g_state.scrollTarget[idx] = 0;
                if (wp == VK_END) g_state.scrollTarget[idx] = g_state.contentH[idx];
            }
            if (wp == VK_ESCAPE) {
                if (g_playing) {
                    tsuzuki::ui::requestStop();
                } else {
                    g_state.screen = tsuzuki::view::Screen::Home;
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PLAYBACK: {
            g_playing = wp != 0;
            if (g_playing) {
                // Remember where we were; the strip replaces the whole
                // interface while the picture is up.
                if (g_state.screen != tsuzuki::view::Screen::Player) {
                    g_state.beforePlayer = g_state.screen;
                }
                g_state.screen = tsuzuki::view::Screen::Player;
            } else if (g_state.screen == tsuzuki::view::Screen::Player) {
                g_state.screen = g_state.beforePlayer;
            }
            layoutVideo(hwnd, g_playing);
            if (g_playing) {
                SetFocus(g_video);
                g_lastActivity = GetTickCount();
                g_barShow = 0.0f;
                g_lastTick = GetTickCount();
                // The default 15.6ms system clock coalesces a 16ms timer down
                // to roughly twelve ticks a second, which is what made the
                // slide look stepped. Ask for 1ms while the video is up, and
                // give it back afterwards - it costs power to hold.
                if (!g_highResTimer && timeBeginPeriod(1) == TIMERR_NOERROR) {
                    g_highResTimer = true;
                }
                SetTimer(hwnd, kOverlayTimer, 8, nullptr);
            } else {
                KillTimer(hwnd, kOverlayTimer);
                if (g_highResTimer) {
                    timeEndPeriod(1);
                    g_highResTimer = false;
                }
            }
            return 0;
        }

        case WM_START_AUTH:
            openAuthWindow(reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                           hwnd);
            return 0;

        case WM_APP + 3:  // token captured
            closeAuthWindow();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 720;
            mmi->ptMinTrackSize.y = 520;
            return 0;
        }

        case WM_DESTROY:
            if (g_highResTimer) {
                timeEndPeriod(1);
                g_highResTimer = false;
            }
            tsuzuki::async::shutdown();
            tsuzuki::images::stop();
            g_overlayCanvas.detach();
            g_canvas.detach();
            // Take the downloads with us on the way out.
            tsuzuki::ui::shutdown();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Pull "access_token=..." out of a URL fragment.
std::wstring tokenFromUrl(const std::wstring& url) {
    const auto at = url.find(L"access_token=");
    if (at == std::wstring::npos) return {};
    std::wstring rest = url.substr(at + 13);
    const auto end = rest.find_first_of(L"&#");
    if (end != std::wstring::npos) rest = rest.substr(0, end);
    return rest;
}

void closeAuthWindow() {
    g_authView.Reset();
    g_authController.Reset();
    if (g_authWnd) {
        DestroyWindow(g_authWnd);
        g_authWnd = nullptr;
    }
}

// Opens AniList in a window we own and watches where it navigates. AniList
// sends the token back in the fragment of the redirect; because we see the
// navigation before it happens, the redirect target never has to load, be
// reachable, or even be correct.
void openAuthWindow(HINSTANCE instance, HWND owner) {
    if (!g_env) return;
    if (g_authWnd) {
        SetForegroundWindow(g_authWnd);
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW ac{};
        ac.cbSize = sizeof(ac);
        ac.lpfnWndProc = [](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
            if (m == WM_SIZE && g_authController) {
                RECT b{};
                GetClientRect(h, &b);
                g_authController->put_Bounds(b);
                return 0;
            }
            if (m == WM_DESTROY) {
                g_authWnd = nullptr;
                return 0;
            }
            return DefWindowProcW(h, m, w, l);
        };
        ac.hInstance = instance;
        ac.hCursor = LoadCursor(nullptr, IDC_ARROW);
        ac.hbrBackground = CreateSolidBrush(RGB(0x0b, 0x16, 0x22));
        ac.lpszClassName = L"TsuzukiAuthWindow";
        ac.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        RegisterClassExW(&ac);
        registered = true;
    }

    g_authWnd = CreateWindowExW(0, L"TsuzukiAuthWindow", L"Sign in to AniList",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                CW_USEDEFAULT, 520, 720, owner, nullptr, instance, nullptr);
    if (!g_authWnd) return;
    useDarkTitleBar(g_authWnd);
    ShowWindow(g_authWnd, SW_SHOW);

    g_env->CreateCoreWebView2Controller(
        g_authWnd,
        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT {
                if (FAILED(r) || !controller || !g_authWnd) return r;
                g_authController = controller;
                g_authController->get_CoreWebView2(&g_authView);

                RECT b{};
                GetClientRect(g_authWnd, &b);
                g_authController->put_Bounds(b);

                EventRegistrationToken token;
                g_authView->add_NavigationStarting(
                    Callback<ICoreWebView2NavigationStartingEventHandler>(
                        [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args)
                            -> HRESULT {
                            LPWSTR uri = nullptr;
                            if (FAILED(args->get_Uri(&uri)) || !uri) return S_OK;
                            const std::wstring url(uri);
                            CoTaskMemFree(uri);

                            const std::wstring tok = tokenFromUrl(url);
                            if (tok.empty()) return S_OK;

                            // Stop before the redirect target loads - it may be
                            // anything at all, and we already have what we came for.
                            args->put_Cancel(TRUE);

                            const int n = WideCharToMultiByte(CP_UTF8, 0, tok.c_str(), -1, nullptr,
                                                              0, nullptr, nullptr);
                            std::string narrow(n > 0 ? n - 1 : 0, '\0');
                            if (n > 0) {
                                WideCharToMultiByte(CP_UTF8, 0, tok.c_str(), -1, narrow.data(), n,
                                                    nullptr, nullptr);
                            }
                            tsuzuki::ui::acceptToken(narrow);
                            if (g_main) PostMessageW(g_main, WM_APP + 3, 0, 0);
                            return S_OK;
                        })
                        .Get(),
                    &token);

                g_authView->Navigate(g_authUrl.c_str());
                return S_OK;
            })
            .Get());
}

void fatal(HWND owner, const wchar_t* text) {
    MessageBoxW(owner, text, L"Tsuzuki", MB_ICONERROR | MB_OK);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCmd) {
    // Lay out in DIPs and let Windows tell us the real scale, rather than
    // being stretched by the compositor on a high-DPI display.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // libtorrent and httplib both use sockets from several threads.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

    if (!tsuzuki::gfx::init()) {
        fatal(nullptr, L"Could not start Direct2D. A graphics driver update may be needed.");
        return 1;
    }

    if (!tsuzuki::ui::startBackground(kPort, savePath())) {
        fatal(nullptr,
              L"Could not start the Tsuzuki engine on port 7654.\n\n"
              L"Another copy is probably already running.");
        return 1;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x0b, 0x0b, 0x10));
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(101));
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"Tsuzuki", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                CW_USEDEFAULT, 1180, 840, nullptr, nullptr, instance, nullptr);
    if (!hwnd) {
        fatal(nullptr, L"Could not create the window.");
        return 1;
    }
    g_main = hwnd;

    if (!g_canvas.attach(hwnd)) {
        fatal(hwnd, L"Could not create the Direct2D render target.");
        return 1;
    }
    tsuzuki::async::init([] {
        if (g_main) PostMessageW(g_main, WM_IMAGE_READY, 0, 0);
    });
    tsuzuki::images::start(&g_canvas, [] {
        if (g_main) PostMessageW(g_main, WM_IMAGE_READY, 0, 0);
    });

    // Plain black child window; mpv is told to render into it with --wid.
    WNDCLASSEXW vc{};
    vc.cbSize = sizeof(vc);
    vc.lpfnWndProc = DefWindowProcW;
    vc.hInstance = instance;
    vc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    vc.lpszClassName = L"TsuzukiVideo";
    RegisterClassExW(&vc);
    g_video = CreateWindowExW(0, L"TsuzukiVideo", nullptr, WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0,
                              hwnd, nullptr, instance, nullptr);
    // Control bar: a layered child so it can fade, created after the video
    // window so it sits above it in the sibling z-order.
    WNDCLASSEXW oc{};
    oc.cbSize = sizeof(oc);
    oc.lpfnWndProc = OverlayProc;
    oc.hInstance = instance;
    oc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    oc.lpszClassName = L"TsuzukiControls";
    if (!RegisterClassExW(&oc)) logLine("RegisterClassExW(controls)", GetLastError());

    g_overlay = CreateWindowExW(0, L"TsuzukiControls", nullptr,
                                WS_CHILD | WS_CLIPCHILDREN, 0, 0, 320, 92, hwnd, nullptr,
                                instance, nullptr);
    if (!g_overlay) {
        logLine("CreateWindowExW(controls)", GetLastError());
    } else if (!g_overlayCanvas.attach(g_overlay)) {
        logLine("overlay canvas attach failed", 0);
    }

    tsuzuki::ui::setVideoHost(g_video, onPlaybackActive);
    tsuzuki::ui::setAuthHook([](const char* url) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, url, -1, nullptr, 0);
        g_authUrl.assign(n > 0 ? n - 1 : 0, L'\0');
        if (n > 0) MultiByteToWideChar(CP_UTF8, 0, url, -1, g_authUrl.data(), n);
        if (g_main) PostMessageW(g_main, WM_START_AUTH, 0, 0);
    });

    useDarkTitleBar(hwnd);
    ShowWindow(hwnd, showCmd);
    UpdateWindow(hwnd);

    // Only needed for the login window; failing to create it must not stop the
    // app, it just means linking will not work until the runtime is present.
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, userDataFolder().c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (SUCCEEDED(result) && env) g_env = env;
                return S_OK;
            })
            .Get());

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    tsuzuki::gfx::shutdown();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
