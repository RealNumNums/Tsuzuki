// Tsuzuki - native application entry point.
//
// A real Win32 window hosting the interface in a WebView2 control. The engine
// (libtorrent, Anitomy, the source layer) runs in-process exactly as it does
// for the CLI; a loopback HTTP server on 127.0.0.1 is only the transport
// between the C++ side and the view, and never leaves the machine.
//
// WebView2 rather than Qt or ImGui: Qt costs a multi-hour vcpkg build and
// ships ~40MB of DLLs, ImGui looks like a debug overlay, and both would mean
// rewriting an interface that already exists. This gets a genuine native
// window with no browser chrome.

#include <windows.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <wrl.h>

#include <WebView2.h>

#include <string>

#include "ui.hpp"

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

namespace {

constexpr int kPort = 7654;
constexpr wchar_t kClassName[] = L"TsuzukiAppWindow";

ComPtr<ICoreWebView2Environment> g_env;
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

// The AniList login window and its view, kept alive while linking.
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

// Called from the engine worker thread, so it only posts.
void onPlaybackActive(bool active) {
    if (g_main) PostMessageW(g_main, WM_PLAYBACK, active ? 1 : 0, 0);
}

// Height of the control strip shown under the video while something plays.
constexpr int kControlBar = 92;

void layout(HWND hwnd, bool playing) {
    RECT b{};
    GetClientRect(hwnd, &b);
    const int w = b.right - b.left;
    const int h = b.bottom - b.top;

    if (playing) {
        // Video on top, interface reduced to a control strip underneath. The
        // WebView2 cannot be layered transparently over a child HWND, so the
        // controls sit below the picture rather than floating on it.
        const int videoH = h > kControlBar ? h - kControlBar : h;
        if (g_video) {
            MoveWindow(g_video, 0, 0, w, videoH, TRUE);
            ShowWindow(g_video, SW_SHOW);
        }
        if (g_controller) {
            RECT bar{0, videoH, w, h};
            g_controller->put_Bounds(bar);
            g_controller->put_IsVisible(TRUE);
        }
    } else {
        if (g_video) ShowWindow(g_video, SW_HIDE);
        if (g_controller) {
            g_controller->put_Bounds(b);
            g_controller->put_IsVisible(TRUE);
        }
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
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                          sizeof(dark));
}

void openAuthWindow(HINSTANCE instance, HWND owner);
void closeAuthWindow();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            layout(hwnd, g_video && IsWindowVisible(g_video));
            return 0;

        case WM_PLAYBACK: {
            const bool playing = wp != 0;
            layout(hwnd, playing);
            if (playing) SetFocus(g_video);
            return 0;
        }

        case WM_START_AUTH:
            openAuthWindow(reinterpret_cast<HINSTANCE>(
                               GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)),
                           hwnd);
            return 0;

        case WM_APP + 3:  // token captured
            closeAuthWindow();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 720;
            mmi->ptMinTrackSize.y = 520;
            return 0;
        }

        case WM_DESTROY:
            // Take the downloads with us on the way out.
            tsuzuki::ui::shutdown();
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
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

                            const int n = WideCharToMultiByte(CP_UTF8, 0, tok.c_str(), -1,
                                                              nullptr, 0, nullptr, nullptr);
                            std::string narrow(n > 0 ? n - 1 : 0, '\0');
                            if (n > 0) {
                                WideCharToMultiByte(CP_UTF8, 0, tok.c_str(), -1, narrow.data(),
                                                    n, nullptr, nullptr);
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
    // libtorrent and httplib both use sockets from several threads.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;

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
    // Matches the page background so resizing never flashes white.
    wc.hbrBackground = CreateSolidBrush(RGB(0x0e, 0x0d, 0x17));
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(101));
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"Tsuzuki", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1180, 840, nullptr,
                                nullptr, instance, nullptr);
    if (!hwnd) {
        fatal(nullptr, L"Could not create the window.");
        return 1;
    }

    g_main = hwnd;

    // Plain black child window; mpv is told to render into it with --wid.
    WNDCLASSEXW vc{};
    vc.cbSize = sizeof(vc);
    vc.lpfnWndProc = DefWindowProcW;
    vc.hInstance = instance;
    vc.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));
    vc.lpszClassName = L"TsuzukiVideo";
    RegisterClassExW(&vc);
    g_video = CreateWindowExW(0, L"TsuzukiVideo", nullptr, WS_CHILD | WS_CLIPCHILDREN,
                              0, 0, 0, 0, hwnd, nullptr, instance, nullptr);
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

    const std::wstring dataFolder = userDataFolder();

    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, dataFolder.c_str(), nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
                if (FAILED(result) || !env) {
                    fatal(hwnd, L"Failed to start the WebView2 environment.");
                    PostQuitMessage(1);
                    return result;
                }
                g_env = env;
                env->CreateCoreWebView2Controller(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [hwnd](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT {
                            if (FAILED(r) || !controller) {
                                fatal(hwnd, L"Failed to create the WebView2 control.");
                                PostQuitMessage(1);
                                return r;
                            }
                            g_controller = controller;
                            g_controller->get_CoreWebView2(&g_webview);

                            ComPtr<ICoreWebView2Settings> settings;
                            if (SUCCEEDED(g_webview->get_Settings(&settings))) {
                                settings->put_AreDefaultContextMenusEnabled(FALSE);
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(TRUE);
                            }

                            RECT bounds{};
                            GetClientRect(hwnd, &bounds);
                            g_controller->put_Bounds(bounds);

                            const std::wstring url =
                                L"http://127.0.0.1:" + std::to_wstring(kPort) + L"/";
                            g_webview->Navigate(url.c_str());
                            return S_OK;
                        })
                        .Get());
                return S_OK;
            })
            .Get());

    if (FAILED(hr)) {
        fatal(hwnd,
              L"The WebView2 runtime is not available.\n\n"
              L"It ships with Windows 11 and current Windows 10. If this "
              L"machine is missing it, install the Evergreen WebView2 Runtime "
              L"from Microsoft and start Tsuzuki again.");
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_webview.Reset();
    g_controller.Reset();
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
