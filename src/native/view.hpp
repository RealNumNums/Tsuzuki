#pragma once

// The interface itself: screens, layout and input.
//
// Immediate mode - each frame lays out and draws in one pass, registering
// clickable rectangles as it goes. There is no widget tree to keep in step
// with the engine, which is the bulk of what a retained UI costs; a screen is
// just a function of the current state.
//
// Drawing is all our own (see gfx), so this looks like a media app rather than
// a tool: rounded cards, cover art, gradient scrims, hover lift.

#include "../settings.hpp"
#include "gfx.hpp"

#include <string>
#include <vector>

namespace tsuzuki::view {

using gfx::Canvas;
using gfx::Color;
using gfx::Font;
using gfx::Rect;

// What the window collected since the last frame.
struct Input {
    float mouseX = -1, mouseY = -1;
    bool mouseDown = false;      // button currently held
    bool mousePressed = false;   // went down this frame
    bool mouseReleased = false;  // came up this frame
    float wheel = 0;             // notches, positive = away from the user
    std::wstring typed;          // characters entered this frame
    int key = 0;                 // VK_ code, 0 if none
    bool ctrl = false;
};

// Immediate-mode frame state. One per frame, handed to the screens.
class Ui {
  public:
    Ui(Canvas& canvas, Input& input) : c(canvas), in(input) {}

    Canvas& c;
    Input& in;

    // Scrolling is applied by the screen when it lays out; this just carries
    // the offset and the measured height so the window can clamp it.
    float scrollY = 0;
    float contentHeight = 0;

    // Registers a clickable region. `id` only has to be unique within a frame.
    bool clickable(int id, const Rect& r);
    bool isHot(int id) const { return hot_ == id; }

    // Nothing above this line can be clicked. Scrolled content passes under
    // the header, where it is clipped away visually - it must not keep
    // taking clicks it no longer appears to be under.
    float hitTop = 0;
    // 0..1, eased - drives the hover lift without a separate animation system.
    float hover(int id) const;

    void beginFrame();
    void endFrame();
    bool wantsAnimation() const { return animating_; }

  private:
    int hot_ = 0;
    int pressed_ = 0;
    std::vector<std::pair<int, float>> hoverAmount_;
    bool animating_ = false;
};

// Which screen is showing. The window owns this; screens ask to change it.
enum class Screen { Home, Results, Episodes, Settings, Player, Message };

// Everything the interface needs that is not in the engine already.
struct State {
    Screen screen = Screen::Home;
    std::wstring query;
    std::wstring episodeWanted;
    bool queryFocused = false;
    int caret = 0;

    std::wstring message;      // for Screen::Message
    std::wstring openMagnet;   // torrent currently shown on the Episodes screen
    int lastAnilistId = 0;

    // Resume prompt, shown over the episode list.
    bool askingResume = false;
    double resumeSeconds = 0;
    double resumeRemaining = 0;
    int resumePercent = 0;
    int resumeEpisode = 0;
    int resumeIndex = -1;

    // Scrolling runs on two numbers per screen: where the wheel has asked to
    // be (target) and where the view actually is (scroll), which eases
    // towards it. contentH is the last measured height, kept so a wheel
    // event can be clamped the moment it arrives rather than a frame later.
    float scroll[6] = {0, 0, 0, 0, 0, 0};
    float scrollTarget[6] = {0, 0, 0, 0, 0, 0};
    float contentH[6] = {0, 0, 0, 0, 0, 0};

    // Settings are edited against a draft and written back a moment after
    // the last change, so holding a key does not rewrite the file each time.
    Settings draft;
    bool draftLoaded = false;
    bool draftDirty = false;
    unsigned draftAt = 0;
    // Which text field has the caret. Zero means none.
    int focusField = 0;
};

// Draws the current screen. Returns true if another frame is wanted soon
// (an animation is running, or something is loading).
bool frame(Ui&, State&);

// Defined in settings_screen.cpp.
bool settingsScreen(Ui&, State&);

// Shared bits the screens use.
std::wstring widen(const std::string&);
std::string narrow(const std::wstring&);
std::wstring formatTime(double seconds);

}  // namespace tsuzuki::view
