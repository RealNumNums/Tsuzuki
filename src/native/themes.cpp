// The palettes the appearance picker offers.
//
// The web interface had these and the Direct2D rebuild lost them; this is them
// back, as data rather than as a stylesheet. Each one is a complete set, so a
// theme can restyle the whole interface without any screen taking part.
//
// Kuro ships as the default: black and white, to match the mascot.

#include "gfx.hpp"

#include <algorithm>

namespace tsuzuki::gfx {
namespace {

// Monochrome, so the accent has to earn its contrast from lightness alone
// rather than from hue. Everything else is a hue applied over the same shape.
const std::vector<Palette> kPalettes = {
    {"kuro", L"Kuro", rgb(0x08080A), rgb(0x0D0D0F), rgb(0x121214), rgb(0x1B1B1E), rgb(0x2A2A2E), rgb(0xF2F2F4),
     rgb(0x8C8C93), rgb(0xE8E8EC), rgb(0xFFFFFF), rgb(0xC8C8CE), rgb(0xA8A8B0), rgb(0xD46A6A),
     rgb(0x000000)},

    {"tsuzuki", L"Tsuzuki", rgb(0x0B0B10), rgb(0x0E0E14), rgb(0x14141C), rgb(0x1A1A24), rgb(0x262633),
     rgb(0xECECF2), rgb(0x8A8A99), rgb(0xFF5C8A), rgb(0xFF9DBB), rgb(0x4AC97E), rgb(0xE0B341),
     rgb(0xE05F5F), rgb(0x000000)},

    {"blackout", L"Blackout", rgb(0x000000), rgb(0x070707), rgb(0x0B0B0B), rgb(0x141414), rgb(0x242424),
     rgb(0xEDEDED), rgb(0x8A8A8A), rgb(0xFFFFFF), rgb(0xD0D0D0), rgb(0x4AC97E), rgb(0xE0B341),
     rgb(0xE05F5F), rgb(0x000000)},

    {"whiteout", L"Whiteout", rgb(0xF4F4F7), rgb(0xFFFFFF), rgb(0xFFFFFF), rgb(0xECECF2), rgb(0xD6D6E0),
     rgb(0x1B1B22), rgb(0x6A6A78), rgb(0x2B2B33), rgb(0x51515E), rgb(0x2E9E5B), rgb(0xA9781C),
     rgb(0xC24545), rgb(0x1B1B22)},

    {"catppuccin", L"Catppuccin", rgb(0x1E1E2E), rgb(0x181825), rgb(0x242438), rgb(0x2D2D44), rgb(0x3B3B55),
     rgb(0xCDD6F4), rgb(0x9399B2), rgb(0xF5C2E7), rgb(0xFAD6EF), rgb(0xA6E3A1), rgb(0xF9E2AF),
     rgb(0xF38BA8), rgb(0x11111B)},

    {"dracula", L"Dracula", rgb(0x282A36), rgb(0x21222C), rgb(0x31333F), rgb(0x3B3D4D), rgb(0x4A4C60),
     rgb(0xF8F8F2), rgb(0x9CA0B0), rgb(0xBD93F9), rgb(0xD6BEFC), rgb(0x50FA7B), rgb(0xF1FA8C),
     rgb(0xFF5555), rgb(0x14161E)},

    {"amber", L"Amber", rgb(0x100D07), rgb(0x0A0805), rgb(0x191307), rgb(0x241B0C), rgb(0x3A2C12),
     rgb(0xF3E7D0), rgb(0xA08A66), rgb(0xFFB13D), rgb(0xFFCE84), rgb(0x8FC96A), rgb(0xFFD166),
     rgb(0xE07A4F), rgb(0x000000)},

    {"lavender", L"Lavender", rgb(0x15101F), rgb(0x100C1A), rgb(0x1E1730), rgb(0x2A2142), rgb(0x3B2F5A),
     rgb(0xEDE6FA), rgb(0x9B8DBD), rgb(0xB07CFF), rgb(0xCEA9FF), rgb(0x7FD8A0), rgb(0xE7C36B),
     rgb(0xE06A8B), rgb(0x0B0714)},
};

const Palette* g_current = &kPalettes.front();

}  // namespace

const std::vector<Palette>& palettes() { return kPalettes; }

const Palette& currentPalette() { return *g_current; }

void useTheme(const std::string& key) {
    const auto it = std::find_if(kPalettes.begin(), kPalettes.end(),
                                 [&](const Palette& p) { return key == p.key; });
    g_current = it == kPalettes.end() ? &kPalettes.front() : &*it;

    const Palette& p = *g_current;
    theme::bg = p.bg;
    theme::panel = p.panel;
    theme::card = p.card;
    theme::cardHover = p.cardHover;
    theme::line = p.line;
    theme::fg = p.fg;
    theme::dim = p.dim;
    theme::accent = p.accent;
    theme::accentSoft = p.accentSoft;
    theme::good = p.good;
    theme::warn = p.warn;
    theme::bad = p.bad;
    theme::shade = p.shade;
}

Color onAccent() {
    // Dark ink on a light accent and the reverse, worked out from the accent
    // itself so a new palette does not need a hand-picked value.
    const Color a = theme::accent;
    const float luma = 0.299f * a.r + 0.587f * a.g + 0.114f * a.b;
    return luma > 0.55f ? rgb(0x0A0A0C) : rgb(0xFFFFFF);
}

}  // namespace tsuzuki::gfx
