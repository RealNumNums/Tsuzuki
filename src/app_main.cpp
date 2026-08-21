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

ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webview;

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

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            if (g_controller) {
                RECT bounds{};
                GetClientRect(hwnd, &bounds);
                g_controller->put_Bounds(bounds);
            }
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = 720;
            mmi->ptMinTrackSize.y = 520;
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
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
