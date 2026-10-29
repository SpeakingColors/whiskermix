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
    int hw;               // first Digiface hardware channel of the row's kind (input, playback or output), 0-based
};

// Strip colour indices shared with the UI palette.
enum Colour { Grey = 0, Green, Purple, Yellow, Red, Orange, Aqua, Pink, Violet };

// The user's Digiface USB setup, as seen in TotalMix FX (2026-10-20). hw = hardware channel confirmed by the
// "all faders" capture (inputs ADAT 1..16 = 0..15, playbacks 0..15 and PH = 32/33, outputs the same numbering).
inline std::vector<StripDef> studioLayout() {
    using R = Row;
    const float off = kNoRoute;
    return {
        // hardware inputs (11 strips)
        {"SYNTH", R::HardwareInput, true, Green, off, "🎹", 0},
        {"B1", R::HardwareInput, false, Grey, off, "🎛️", 2},
        {"CAT", R::HardwareInput, false, Grey, off, "🐱", 3},
        {"TURNTABLE", R::HardwareInput, true, Grey, off, "💿", 4},
        {"PRO 1", R::HardwareInput, false, Grey, off, "🎚️", 6},
        {"ADAT 8", R::HardwareInput, false, Grey, off, "", 7},
        {"EXCITER", R::HardwareInput, true, Purple, off, "✨", 8},
        {"DEP5", R::HardwareInput, true, Yellow, off, "🌊", 10},
        {"FX 3D", R::HardwareInput, true, Grey, off, "🌀", 12},
        {"PREAMP", R::HardwareInput, false, Red, off, "🎤", 14},
        {"ART VC", R::HardwareInput, false, Orange, off, "🎙️", 15},
        // software playback (9 strips)
        {"COMPUTER", R::SoftwarePlayback, true, Grey, 0.0f, "🖥", 0},
        {"BROWSER", R::SoftwarePlayback, true, Grey, 0.0f, "🌐", 2},
        {"ADAT 5/6", R::SoftwarePlayback, true, Grey, off, "", 4},
        {"ADAT 7/8", R::SoftwarePlayback, true, Grey, off, "", 6},
        {"ADAT 9/10", R::SoftwarePlayback, true, Grey, off, "", 8},
        {"ADAT 11/12", R::SoftwarePlayback, true, Grey, off, "", 10},
        {"ADAT 13/14", R::SoftwarePlayback, true, Grey, off, "", 12},
        {"ADAT 15/16", R::SoftwarePlayback, true, Grey, off, "", 14},
        {"PH", R::SoftwarePlayback, true, Grey, off, "🎧", 32},
        // hardware outputs (8 strips)
        {"ADAT 5/6", R::HardwareOutput, true, Grey, 0.0f, "", 4},
        {"ADAT 7/8", R::HardwareOutput, true, Grey, 0.0f, "", 6},
        {"EXCITER", R::HardwareOutput, true, Purple, 0.0f, "✨", 8},
        {"DEP5", R::HardwareOutput, true, Yellow, 0.0f, "🌊", 10},
        {"FX 3D", R::HardwareOutput, true, Grey, 0.0f, "🌀", 12},
        {"INSERT", R::HardwareOutput, false, Red, 0.0f, "", 14},
        {"PREAMP", R::HardwareOutput, false, Orange, 0.0f, "🎤", 15},
        {"PH", R::HardwareOutput, true, Grey, -10.0f, "🎧", 32},
        // control room (2 strips): Main is bus 0 (the mix every strip plays to), iLoud Micro is Speaker B (bus 1)
        {"Main", R::ControlRoom, true, Grey, -42.1f, "🔊", 0},
        {"iLoud Micro", R::ControlRoom, true, Grey, -27.6f, "🔉", 2},
    };
}

inline int countRow(const std::vector<StripDef>& l, Row r) {
    int n = 0;
    for (const auto& s : l) if (s.row == r) ++n;
    return n;
}

// Builds the mixer and routing from a layout: inputs and playbacks become channels (in that order); the control
// room strips become the first buses (Main = bus 0, Speaker B = bus 1), the hardware outputs follow. Every strip
// starts routed to Main at 0 dB like a fresh TotalMix submix, except strips that start at -inf.
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
    auto busStrips = [&](Row r) { for (const auto& s : l) if (s.row == r) { auto& bus = b.mixer.state().buses[size_t(u)]; bus.name = s.name; bus.volumeDb = s.gainDb <= kNoRoute ? -120.0f : s.gainDb; bus.hwFirst = s.hw; bus.stereo = s.stereo; bus.controlRoom = r == Row::ControlRoom; ++u; } };
    busStrips(Row::ControlRoom); busStrips(Row::HardwareOutput);
    for (const auto& s : l) {
        if (s.row == Row::HardwareInput || s.row == Row::SoftwarePlayback) {
            auto& ch = b.mixer.state().channels[size_t(c)];
            ch.name = s.name; ch.stereoLinked = s.stereo; ch.colour = s.colour; ch.emoji = s.emoji;
            ch.gainDb = s.gainDb <= kNoRoute ? -120.0f : s.gainDb;
            ch.hwFirst = s.hw; ch.playback = s.row == Row::SoftwarePlayback;
            if (b.routing.outputs() > 0 && s.gainDb > kNoRoute) b.routing.set(c, 0, 0.0f);   // plays to Main
            ++c;
        }
    }
    return b;
}

}  // namespace wm::mixer
