#include "player.hpp"

#include <nlohmann/json.hpp>

#include <mutex>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tsuzuki::player {
namespace {

using nlohmann::json;

// mpv prepends \\.\pipe\ to whatever --input-ipc-server is handed to it, so the
// option takes the bare name and only our side of the connection spells out
// the full path. Passing the full path made mpv listen on
// \\.\pipe\\\.\pipe\tsuzuki-mpv instead: no error, and no socket where
// anyone was looking. Every state() call has failed quietly ever since, and
// that one wrong string is what disabled the player controls, the rolling
// window, resume points and AniList progress sync all at once.
constexpr const char* kPipeName = "tsuzuki-mpv";
constexpr const char* kPipePath = R"(\\.\pipe\tsuzuki-mpv)";

std::mutex g_mutex;
std::string g_pending;  // bytes read but not yet consumed
int g_requestId = 0;

#ifdef _WIN32
HANDLE g_pipe = INVALID_HANDLE_VALUE;

void disconnect() {
    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
    g_pending.clear();
}

bool ensureConnected() {
    if (g_pipe != INVALID_HANDLE_VALUE) return true;
    g_pipe = CreateFileA(kPipePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                         0, nullptr);
    if (g_pipe == INVALID_HANDLE_VALUE) return false;

    DWORD mode = PIPE_READMODE_BYTE;
    SetNamedPipeHandleState(g_pipe, &mode, nullptr, nullptr);
    g_pending.clear();
    return true;
}

// One line of JSON from mpv, or empty on timeout/failure. mpv interleaves
// unsolicited events with command replies, so callers filter by request_id.
std::string readLine(int timeoutMs) {
    const DWORD deadline = GetTickCount() + static_cast<DWORD>(timeoutMs);
    for (;;) {
        const auto nl = g_pending.find('\n');
        if (nl != std::string::npos) {
            std::string line = g_pending.substr(0, nl);
            g_pending.erase(0, nl + 1);
            return line;
        }
        if (GetTickCount() > deadline) return {};

        DWORD available = 0;
        if (!PeekNamedPipe(g_pipe, nullptr, 0, nullptr, &available, nullptr)) {
            disconnect();
            return {};
        }
        if (available == 0) {
            Sleep(10);
            continue;
        }
        char buf[4096];
        DWORD got = 0;
        const DWORD want = available > sizeof(buf) ? (DWORD)sizeof(buf) : available;
        if (!ReadFile(g_pipe, buf, want, &got, nullptr) || got == 0) {
            disconnect();
            return {};
        }
        g_pending.append(buf, got);
    }
}

json request(const json& command, int timeoutMs = 700) {
    if (!ensureConnected()) return json();

    const int id = ++g_requestId;
    json message = command;
    message["request_id"] = id;
    const std::string line = message.dump() + "\n";

    DWORD written = 0;
    if (!WriteFile(g_pipe, line.data(), (DWORD)line.size(), &written, nullptr)) {
        disconnect();
        return json();
    }

    for (;;) {
        const std::string reply = readLine(timeoutMs);
        if (reply.empty()) return json();
        try {
            json j = json::parse(reply);
            if (j.contains("request_id") && j["request_id"].get<int>() == id) return j;
        } catch (const std::exception&) {
            // mpv occasionally emits non-JSON noise; ignore and keep reading.
        }
    }
}

json getProperty(const char* name) {
    const json r = request({{"command", {"get_property", name}}});
    if (r.is_null() || !r.contains("data")) return json();
    return r["data"];
}

void setProperty(const char* name, const json& value) {
    request({{"command", {"set_property", name, value}}});
}

#else   // non-Windows: IPC unimplemented, everything is a no-op
bool ensureConnected() { return false; }
json request(const json&, int = 700) { return json(); }
json getProperty(const char*) { return json(); }
void setProperty(const char*, const json&) {}
void disconnect() {}
#endif

}  // namespace

std::string pipeName() { return kPipeName; }

bool connected() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ensureConnected();
}

State state() {
    std::lock_guard<std::mutex> lock(g_mutex);
    State s;
    if (!ensureConnected()) return s;

    const json pause = getProperty("pause");
    if (pause.is_null()) {
        // mpv is gone; drop the handle so the next call reconnects cleanly.
        disconnect();
        return s;
    }

    s.running = true;
    s.paused = pause.is_boolean() ? pause.get<bool>() : false;

    const json pos = getProperty("time-pos");
    if (pos.is_number()) s.position = pos.get<double>();
    const json dur = getProperty("duration");
    if (dur.is_number()) s.duration = dur.get<double>();
    const json vol = getProperty("volume");
    if (vol.is_number()) s.volume = static_cast<int>(vol.get<double>());
    const json subVis = getProperty("sub-visibility");
    if (subVis.is_boolean()) s.subsVisible = subVis.get<bool>();

    const json tracks = getProperty("track-list");
    if (tracks.is_array()) {
        for (const auto& t : tracks) {
            Track track;
            track.id = t.value("id", 0);
            track.type = t.value("type", "");
            track.title = t.value("title", "");
            track.lang = t.value("lang", "");
            track.codec = t.value("codec", "");
            track.selected = t.value("selected", false);
            track.isDefault = t.value("default", false);
            if (track.type == "audio" || track.type == "sub") {
                s.tracks.push_back(std::move(track));
            }
        }
    }
    return s;
}

void togglePause() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureConnected()) return;
    const json p = getProperty("pause");
    setProperty("pause", p.is_boolean() ? !p.get<bool>() : true);
}

void setPaused(bool paused) {
    std::lock_guard<std::mutex> lock(g_mutex);
    setProperty("pause", paused);
}

void seekRelative(double seconds) {
    std::lock_guard<std::mutex> lock(g_mutex);
    request({{"command", {"seek", seconds, "relative"}}});
}

void seekAbsolute(double seconds) {
    std::lock_guard<std::mutex> lock(g_mutex);
    request({{"command", {"seek", seconds, "absolute"}}});
}

void setAudioTrack(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    setProperty("aid", id <= 0 ? json("no") : json(id));
}

void setSubTrack(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    setProperty("sid", id <= 0 ? json("no") : json(id));
}

void setSubsVisible(bool visible) {
    std::lock_guard<std::mutex> lock(g_mutex);
    setProperty("sub-visibility", visible);
}

void setVolume(int percent) {
    std::lock_guard<std::mutex> lock(g_mutex);
    setProperty("volume", percent);
}

void stop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ensureConnected()) return;
    request({{"command", {"quit"}}}, 300);
    disconnect();
}

}  // namespace tsuzuki::player
