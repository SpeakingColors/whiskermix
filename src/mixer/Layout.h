// Channel layout: which strips exist, in which row (hardware inputs, software playback, hardware outputs,
// control room), with the names, colours, stereo links and starting faders the user had in TotalMix.
// Pure C++. The layout is also what Settings > Channel layout edits.
#pragma once
#include <string>
#include <vector>
#include "MixerModel.h"
#include "RoutingMatrix.h"

namespace wm::mixer {

enum class Row { HardwareInput, SoftwarePlayback, HardwareOutput, ControlRoom };

struct StripDef {
    std::string name;
    Row row;
    bool stereo;          // stereo pair (two hardware channels, one strip)
    int colour;           // index into the 9 soda pop strip colours (0 = none/grey)
    float gainDb;         // starting fader; kNoRoute (-120) = -inf
    std::string emoji;
};

// Strip colour indices shared with the UI palette.
enum Colour { Grey = 0, Green, Purple, Yellow, Red, Orange, Aqua, Pink, Violet };

// The user's Digiface USB setup, as seen in TotalMix FX (2026-10-20).
inline std::vector<StripDef> studioLayout() {
    using R = Row;
    const float off = kNoRoute;
    return {
        // hardware inputs (11 strips)
        {"SYNTH", R::HardwareInput, true, Green, off, "🎹"},
        {"B1", R::HardwareInput, false, Grey, off, "🎛️"},
        {"CAT", R::HardwareInput, false, Grey, off, "🐱"},
        {"TURNTABLE", R::HardwareInput, true, Grey, off, "💿"},
        {"PRO 1", R::HardwareInput, false, Grey, off, "🎚️"},
        {"ADAT 8", R::HardwareInput, false, Grey, off, ""},
        {"EXCITER", R::HardwareInput, true, Purple, off, "✨"},
        {"DEP5", R::HardwareInput, true, Yellow, off, "🌊"},
        {"FX 3D", R::HardwareInput, true, Grey, off, "🌀"},
        {"PREAMP", R::HardwareInput, false, Red, off, "🎤"},
        {"ART VC", R::HardwareInput, false, Orange, off, "🎙️"},
        // software playback (9 strips)
        {"COMPUTER", R::SoftwarePlayback, true, Grey, 0.0f, "🖥"},
        {"BROWSER", R::SoftwarePlayback, true, Grey, 0.0f, "🌐"},
        {"ADAT 5/6", R::SoftwarePlayback, true, Grey, off, ""},
        {"ADAT 7/8", R::SoftwarePlayback, true, Grey, off, ""},
        {"ADAT 9/10", R::SoftwarePlayback, true, Grey, off, ""},
        {"ADAT 11/12", R::SoftwarePlayback, true, Grey, off, ""},
        {"ADAT 13/14", R::SoftwarePlayback, true, Grey, off, ""},
        {"ADAT 15/16", R::SoftwarePlayback, true, Grey, off, ""},
        {"PH", R::SoftwarePlayback, false, Grey, off, "🎧"},
        // hardware outputs (8 strips)
        {"ADAT 5/6", R::HardwareOutput, true, Grey, 0.0f, ""},
        {"ADAT 7/8", R::HardwareOutput, true, Grey, 0.0f, ""},
        {"EXCITER", R::HardwareOutput, true, Purple, 0.0f, "✨"},
        {"DEP5", R::HardwareOutput, true, Yellow, 0.0f, "🌊"},
        {"FX 3D", R::HardwareOutput, true, Grey, 0.0f, "🌀"},
        {"INSERT", R::HardwareOutput, false, Red, 0.0f, ""},
        {"PREAMP", R::HardwareOutput, false, Orange, 0.0f, "🎤"},
        {"PH", R::HardwareOutput, true, Grey, -10.0f, "🎧"},
        // control room (2 strips)
        {"iLoud Micro", R::ControlRoom, true, Grey, -27.6f, "🔉"},
        {"Main", R::ControlRoom, true, Grey, -42.1f, "🔊"},
    };
}

inline int countRow(const std::vector<StripDef>& l, Row r) {
    int n = 0;
    for (const auto& s : l) if (s.row == r) ++n;
    return n;
}

// Builds the mixer and routing from a layout: inputs and playbacks become channels (in that order),
// hardware outputs and control room become buses (in that order). Every strip starts routed to the first bus
// (Main mix) at 0 dB like a fresh TotalMix submix, except strips that start at -inf.
struct Built {
    Mixer mixer;
    RoutingMatrix routing;
    int inputs = 0, playbacks = 0, outputs = 0, controlRoom = 0;
    Built(int ch, int bus) : mixer(ch, bus), routing(ch, bus) {}
};

inline Built buildFromLayout(const std::vector<StripDef>& l) {
    const int in = countRow(l, Row::HardwareInput), pb = countRow(l, Row::SoftwarePlayback);
    const int out = countRow(l, Row::HardwareOutput), cr = countRow(l, Row::ControlRoom);
    Built b(in + pb, out + cr);
    b.inputs = in; b.playbacks = pb; b.outputs = out; b.controlRoom = cr;
    int c = 0, u = 0;
    for (const auto& s : l) {
        if (s.row == Row::HardwareInput || s.row == Row::SoftwarePlayback) {
            auto& ch = b.mixer.state().channels[size_t(c)];
            ch.name = s.name; ch.stereoLinked = s.stereo; ch.colour = s.colour; ch.emoji = s.emoji;
            ch.gainDb = s.gainDb <= kNoRoute ? -120.0f : s.gainDb;
            // route to the main bus (the last bus = Main in the control room) at the strip's fader
            if (b.routing.outputs() > 0 && s.gainDb > kNoRoute) b.routing.set(c, b.routing.outputs() - 1, 0.0f);
            ++c;
        } else {
            auto& bus = b.mixer.state().buses[size_t(u)];
            bus.name = s.name; bus.volumeDb = s.gainDb <= kNoRoute ? -120.0f : s.gainDb;
            ++u;
        }
    }
    return b;
}

}  // namespace wm::mixer
