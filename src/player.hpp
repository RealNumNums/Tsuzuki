#pragma once

#include <string>
#include <vector>

namespace tsuzuki::player {

// Controls the running mpv instance over its JSON IPC socket.
//
// mpv has all of this on its own keybindings, but expecting someone to know
// that '#' cycles audio and 'v' toggles subtitles is not an interface. Driving
// it over IPC lets the app show real controls.

struct Track {
    int id = 0;
    std::string type;   // "audio" | "sub" | "video"
    std::string title;
    std::string lang;
    std::string codec;
    bool selected = false;
    bool isDefault = false;
};

struct State {
    bool running = false;
    bool paused = false;
    double position = 0;   // seconds
    double duration = 0;   // seconds
    int volume = 100;
    bool subsVisible = true;
    std::vector<Track> tracks;
};

// Pipe mpv should listen on. Passed to mpv as --input-ipc-server.
std::string pipeName();

// True once mpv has opened the pipe.
bool connected();

State state();

void togglePause();
void setPaused(bool paused);
void seekRelative(double seconds);
void seekAbsolute(double seconds);
void setAudioTrack(int id);
void setSubTrack(int id);   // id 0 disables subtitles
void setSubsVisible(bool visible);
void setVolume(int percent);
void stop();

}  // namespace tsuzuki::player
