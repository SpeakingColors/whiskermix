// ADAT channel layout of the RME Digiface USB and the Ferrofish Pulse 16 DX: how many channels each optical port
// carries at each sample rate (S/MUX), which port a channel sits on, and which strips exist at a given rate.
// Pure C++. Sources: RME Digiface USB User's Guide (chapters 8.3 / 9.2 / 25: channel counts and ASIO channel names),
// Ferrofish PULSE16 DX Users Guide v2.0c ("I/Os - ADAT"). See docs/ROUTING-RME-FERROFISH.md for what is documented,
// assumed and untested.
#pragma once
#include <vector>
#include "Layout.h"

namespace wm::mixer {

enum class Speed { Single, Double, Quad };   // normal rate, S/MUX (double speed), S/MUX4 (quad speed)

// Documented ranges: Single up to 48 kHz, Double 88.2/96 kHz, Quad 176.4/192 kHz.
inline Speed speedForRate(int hz) { return hz <= 50000 ? Speed::Single : hz <= 100000 ? Speed::Double : Speed::Quad; }
inline int adatChannelsPerPort(Speed s) { return s == Speed::Single ? 8 : s == Speed::Double ? 4 : 2; }
inline int adatPorts() { return 4; }                                   // Digiface USB: 4 optical in + 4 out
inline int adatChannelsTotal(Speed s) { return adatPorts() * adatChannelsPerPort(s); }   // 32 / 16 / 8

struct AdatSlot { int port = 0, slot = 0; };   // port 0..3 (optical 1..4), slot inside that port

// The Digiface channel names (ASIO "ADAT (1-8), (9-16)..." at single speed, "(1-4), (5-8)..." at double speed,
// "(1+2), (3+4)..." at quad speed) fill port 1 first, then port 2, and so on. The port order is implied by those names;
// not checked with a cable test (see docs).
inline bool adatSlotFor(int ch0, Speed s, AdatSlot& out) {
    if (ch0 < 0 || ch0 >= adatChannelsTotal(s)) return false;
    out.port = ch0 / adatChannelsPerPort(s); out.slot = ch0 % adatChannelsPerPort(s); return true;
}
inline int adatFirstChannelOfPort(int port, Speed s) { return port * adatChannelsPerPort(s); }

// Pulse 16 DX routing column for an ADAT port: ADAT IN 1-8 .. 25-32 are columns 2..5 (owner capture, PULSE16-PROTOCOL.md).
inline int pulseAdatColumn(int port) { return 2 + port; }

// Phones playback/output (hardware channels 32 and 33) is analog and exists at every rate.
inline bool hwChannelAvailable(int hw, Speed s) { return hw >= 32 || hw < adatChannelsTotal(s); }

// A strip is usable at a rate when all its hardware channels exist (stereo strips need hw and hw+1).
inline bool stripAvailable(const StripDef& d, Speed s) {
    if (d.hw < 0) return true;
    return hwChannelAvailable(d.hw, s) && (!d.stereo || hwChannelAvailable(d.hw + 1, s));
}
inline std::vector<StripDef> layoutForSpeed(const std::vector<StripDef>& l, Speed s) {
    std::vector<StripDef> r;
    for (const auto& d : l) if (stripAvailable(d, s)) r.push_back(d);
    return r;
}

}  // namespace wm::mixer
