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
#include "../ui.hpp"
#include "gfx.hpp"

#include <map>
#include <set>
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

    // Moves the whole frame - drawing, reported bounds and the mouse - so a
    // screen can be written as though the window started at (x, y). The
    // navigation rail uses it; screens are unaware of it.
    void pushOrigin(float x, float y);
    void popOrigin();

    // The pointer in the current origin's coordinates. Anything hit-testing by
    // hand rather than through clickable() wants these, not in.mouseX/Y.
    float mouseX() const { return in.mouseX - originX_; }
    float mouseY() const { return in.mouseY - originY_; }
    // 0..1, eased - drives the hover lift without a separate animation system.
    float hover(int id) const;

    void beginFrame();
    void endFrame();
    bool wantsAnimation() const { return animating_; }

  private:
    float originX_ = 0, originY_ = 0;
    int hot_ = 0;
    int pressed_ = 0;
    std::vector<std::pair<int, float>> hoverAmount_;
    bool animating_ = false;
};

// The clipboard as text, empty when it holds something else. Ctrl+V arrives
// as an unprintable control character rather than as the pasted text, so
// every editable field has to go and fetch it.
std::wstring clipboardText();

// What Windows puts in the character stream for Ctrl+V. Reading the paste out
// of the typed characters avoids asking GetKeyState what the keyboard was
// doing, which is a different question from what was typed into this frame.
inline constexpr wchar_t kPasteChar = 0x16;

// Which screen is showing. The window owns this; screens ask to change it.
enum class Screen { Home, Results, Episodes, Settings, Player, Discover, Shelf, Schedule };

// Everything the interface needs that is not in the engine already.
struct State {
    Screen screen = Screen::Home;

    // The companion window. The view only flips this; the window itself is
    // the application's business, since it is the one holding the HWND.
    bool mascotOn = false;
    std::wstring query;
    std::wstring episodeWanted;

    // Which episode the results are filtered to. Zero shows every release,
    // -1 shows only batches. Results arrive sorted by seeders, so without
    // this the early episodes of a running show are buried under the latest.
    int resultsEpisode = 0;
    int pendingEpisode = 0;  // the episode an in-flight narrowed search is for
    bool resultsEpisodeChosen = false;
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
    float scroll[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float scrollTarget[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    float contentH[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    // Settings are edited against a draft and written back a moment after
    // the last change, so holding a key does not rewrite the file each time.
    Settings draft;
    bool draftLoaded = false;
    bool draftDirty = false;
    unsigned draftAt = 0;
    // Which text field has the caret. Zero means none.
    int focusField = 0;

    // Answers from the last search and the last torrent opened. Kept here
    // rather than re-fetched per frame - both cost seconds on the network.
    ui::SearchOutcome results;
    bool searchDone = false;
    ui::OpenOutcome opened;
    bool openDone = false;

    // Discover: shelves of trending and popular titles, and which genre
    // they are narrowed to. Empty genre means all.
    ui::Discovery discovery;
    bool discoverLoaded = false;
    std::wstring genre;

    // A single shelf opened in full, paged as it is scrolled.
    std::string openShelf;
    std::wstring openShelfTitle;
    std::vector<ui::DiscoverItem> openShelfItems;
    int openShelfPage = 1;
    bool openShelfExhausted = false;

    // The show the home banner leads with, and which show it describes.
    ui::Spotlight hero;
    int heroWant = 0;   // the show we would like the banner to be about
    int heroAsked = 0;  // the show we have already asked AniList about

    // Airing calendar: which month is shown, and what airs in it.
    int calYear = 0;
    int calMonth = 0;
    // Your own shows by default: a calendar of everything airing is mostly
    // noise, and asking only about your list is one request instead of sixteen.
    bool calMineOnly = true;
    int calShownKey = -1;    // which month+mode `airing` actually holds
    int calPendingKey = -1;  // which month+mode the in-flight request is for
    std::vector<ui::AiringEntry> airing;

    // A month costs several requests to assemble, so paging back to one you
    // have already looked at should not spend them again.
    std::map<int, std::vector<ui::AiringEntry>> calCache;
    std::set<int> calDone;  // keys whose fetch ran to the end, so partial
                            // results from an abandoned month are not reused

    // Linking a tracker. Simkl shows a code to type in on its site; Kitsu
    // has no consent page and asks here. Both need somewhere to live while
    // the exchange is in progress.
    std::string linking;        // service id, empty when nothing is in progress
    std::wstring deviceCode;
    std::wstring deviceUrl;
    std::wstring kitsuUser;
    std::wstring kitsuPassword;
    std::wstring linkError;
    unsigned lastPoll = 0;

    // Where to go back to once playback ends. Playback takes over the whole
    // window, so the screen behind it has to be remembered.
Screen beforePlayer = Screen::Home;
};

// Draws the current screen. Returns true if another frame is wanted soon
// (an animation is running, or something is loading).
bool frame(Ui&, State&);

// Defined in settings_screen.cpp.
bool settingsScreen(Ui&, State&);

// Defined in browse_screens.cpp.
bool resultsScreen(Ui&, State&);
bool episodesScreen(Ui&, State&);

// Defined in discover.cpp.
bool discoverScreen(Ui&, State&);
bool shelfScreen(Ui&, State&);
bool scheduleScreen(Ui&, State&);

// Defined in waiting.cpp - the panel shown while an episode gets ready.
bool waitingPanel(Ui&, State&, const std::wstring& heading);

// Defined in player_strip.cpp.
bool playerStrip(Ui&, State&);

// Shared bits the screens use.
std::wstring widen(const std::string&);
std::string narrow(const std::wstring&);
std::wstring formatTime(double seconds);

}  // namespace tsuzuki::view
