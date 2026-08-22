#include "discord.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tsuzuki::discord {
namespace {

using nlohmann::json;

std::mutex g_mutex;
std::string g_clientId;

// Registered as "Tsuzuki" at discord.com/developers. Public by design; see
// the note in the header.
constexpr const char* kDefaultClientId = "1540524110235303936";

#ifdef _WIN32
HANDLE g_pipe = INVALID_HANDLE_VALUE;

void closePipe() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

// Frames are a 4-byte little-endian opcode, a 4-byte length, then JSON.
bool writeFrame(std::int32_t opcode, const std::string& payload) {
    if (g_pipe == INVALID_HANDLE_VALUE) return false;

    std::string frame;
    frame.resize(8 + payload.size());
    const std::int32_t len = static_cast<std::int32_t>(payload.size());
    std::memcpy(&frame[0], &opcode, 4);
    std::memcpy(&frame[4], &len, 4);
    std::memcpy(&frame[8], payload.data(), payload.size());

    DWORD written = 0;
    if (!WriteFile(g_pipe, frame.data(), (DWORD)frame.size(), &written, nullptr)) {
        closePipe();
        return false;
    }
    return true;
}

// Discord's reply is not needed, but leaving it in the pipe stalls later
// writes, so drain whatever is waiting.
void drain() {
    if (g_pipe == INVALID_HANDLE_VALUE) return;
    DWORD available = 0;
    if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &available, nullptr)) return;
    while (available > 0) {
        char buf[2048];
        DWORD got = 0;
        const DWORD want = available > sizeof(buf) ? (DWORD)sizeof(buf) : available;
        if (!ReadFile(g_pipe, buf, want, &got, nullptr) || got == 0) return;
        available -= got;
    }
}

bool openPipe(const std::string& clientId) {
    if (g_pipe != INVALID_HANDLE_VALUE) return true;
    if (clientId.empty()) return false;

    // Discord numbers its pipes when several clients are installed.
    for (int i = 0; i < 10; ++i) {
        const std::string name = R"(\\.\pipe\discord-ipc-)" + std::to_string(i);
        HANDLE h = CreateFileA(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            g_pipe = h;
            const json hello{{"v", 1}, {"client_id", clientId}};
            if (!writeFrame(0, hello.dump())) return false;
            drain();
            return true;
        }
    }
    return false;
}
#else
bool openPipe(const std::string&) { return false; }
bool writeFrame(std::int32_t, const std::string&) { return false; }
void closePipe() {}
void drain() {}
#endif

}  // namespace

std::string defaultClientId() { return kDefaultClientId; }

std::string resolveClientId(const std::string& fromSettings) {
    return fromSettings.empty() ? defaultClientId() : fromSettings;
}

bool connect(const std::string& clientId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_clientId = clientId;
    return openPipe(clientId);
}

bool connected() {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_pipe != INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}

void setWatching(const Presence& p) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!openPipe(g_clientId)) return;

    json activity;
    activity["details"] = p.show.empty() ? "Browsing" : p.show;
    if (!p.episode.empty()) {
        activity["state"] = (p.paused ? "Paused - " : "") + p.episode;
    }

    // An end timestamp makes Discord count down the time left, which is far
    // more use for an episode than counting up how long it has been open.
    // Paused shows neither: a frozen clock would just be wrong.
    activity["timestamps"] = json::object();
    if (!p.paused && !p.show.empty() && p.remaining > 1) {
        activity["timestamps"]["end"] =
            static_cast<std::int64_t>(std::time(nullptr) + static_cast<long long>(p.remaining));
    }

    // The cover goes in the large slot as a plain URL - modern Discord
    // clients resolve external images in RPC assets - with the app logo
    // demoted to the small badge so it still says what is playing this. If
    // there is no cover, the logo takes the large slot instead.
    json assets;
    if (!p.imageUrl.empty()) {
        assets["large_image"] = p.imageUrl;
        assets["large_text"] = p.show.empty() ? "Tsuzuki" : p.show;
        assets["small_image"] = "tsuzuki";
        assets["small_text"] = "Tsuzuki";
    } else {
        assets["large_image"] = "tsuzuki";
        assets["large_text"] = "Tsuzuki";
    }
    activity["assets"] = assets;

    const json frame{{"cmd", "SET_ACTIVITY"},
                     {"nonce", std::to_string(std::time(nullptr))},
                     {"args", {{"pid", static_cast<int>(
#ifdef _WIN32
                                          GetCurrentProcessId()
#else
                                          0
#endif
                                              )},
                               {"activity", activity}}}};
    writeFrame(1, frame.dump());
    drain();
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_clientId.empty()) return;
#ifdef _WIN32
    if (g_pipe == INVALID_HANDLE_VALUE) return;
#endif
    const json frame{{"cmd", "SET_ACTIVITY"},
                     {"nonce", std::to_string(std::time(nullptr))},
                     {"args", {{"pid", static_cast<int>(
#ifdef _WIN32
                                          GetCurrentProcessId()
#else
                                          0
#endif
                                              )}}}};
    writeFrame(1, frame.dump());
    drain();
}

void disconnect() {
    std::lock_guard<std::mutex> lock(g_mutex);
    closePipe();
}

}  // namespace tsuzuki::discord
