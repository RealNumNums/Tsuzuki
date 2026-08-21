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
#include <windowsx.h>
#include <shellapi.h>
#include <wrl.h>

#include <WebView2.h>

#include <cmath>
#include <string>

#include "native/gfx.hpp"
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

tsuzuki::gfx::Canvas g_canvas;
tsuzuki::view::State g_state;
tsuzuki::view::Input g_input;
// One Ui for the life of the window, not one per frame. It carries which
// element is being pressed and how far each hover has eased in - rebuilding it
// each frame reset both, so a press and its release landed on different
// objects and no click was ever recognised.
tsuzuki::view::Ui g_ui(g_canvas, g_input);
bool g_playing = false;
bool g_animating = false;

// Called from the engine worker thread, so it only posts.
void onPlaybackActive(bool active) {
    if (g_main) PostMessageW(g_main, WM_PLAYBACK, active ? 1 : 0, 0);
}

// Height of the control strip shown under the video while something plays.
constexpr int kControlBar = 92;

void layoutVideo(HWND hwnd, bool playing) {
    RECT b{};
    GetClientRect(hwnd, &b);
    const int w = b.right - b.left;
    const int h = b.bottom - b.top;

    if (playing && g_video) {
        const int videoH = h > kControlBar ? h - kControlBar : h;
        MoveWindow(g_video, 0, 0, w, videoH, TRUE);
        ShowWindow(g_video, SW_SHOW);
    } else if (g_video) {
        ShowWindow(g_video, SW_HIDE);
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void render(HWND hwnd) {
    using namespace tsuzuki;

    bool scrolling = false;

    if (!g_canvas.begin(gfx::theme::bg)) return;

    // While the video is up, the interface is only the strip underneath it -
    // the picture belongs to a child window and must not be painted over.
    const gfx::Rect full = g_canvas.bounds();
    if (g_playing) {
        const float barTop = full.h - kControlBar;
        g_canvas.pushClip({0, barTop, full.w, static_cast<float>(kControlBar)});
    }

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

    if (g_playing) g_canvas.popClip();
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

void openAuthWindow(HINSTANCE instance, HWND owner);
void closeAuthWindow();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            g_canvas.resize(LOWORD(lp), HIWORD(lp));
            layoutVideo(hwnd, g_playing);
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
                g_state.screen = tsuzuki::view::Screen::Home;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_PLAYBACK: {
            g_playing = wp != 0;
            layoutVideo(hwnd, g_playing);
            if (g_playing) SetFocus(g_video);
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
            tsuzuki::images::stop();
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
