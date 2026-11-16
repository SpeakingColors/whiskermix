// ADAT routing rules for one RME Digiface USB and one Ferrofish Pulse 16 DX on a Linux (ALSA / PipeWire) system.
// Pure C++, no hardware or PipeWire calls: it only computes channel counts, names, clock plans and link commands.
//
// Sources and status (see docs/ROUTING.md):
//  - Digiface USB: 4 optical ports, each ADAT (8 ch at 44.1/48 kHz, 4 ch at 88.2/96 kHz S/MUX, 2 ch at 176.4/192 kHz S/MUX4)
//    or S/PDIF (stereo). Totals 32 / 16 / 8 ADAT channels. Taken from RME's Digiface USB product page and a mirror of the
//    User's Guide as seen through search results; the PDF itself could not be opened when this was written (DOCUMENTED, to be re-read).
//  - Which hardware channels stay active at the higher rates is ASSUMED (the first ones). Check with tools/wm-hw-routing.sh.
//  - Nothing here has been run against real hardware (UNTESTED).
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>
#include "../backends/pulse16/Pulse16Codec.h"

namespace wm::routing {

enum class RateClass { Unsupported = 0, Single = 1, Double = 2, Quad = 3 };

// Sample rates the Digiface USB lists: 44.1 to 192 kHz in three classes. 32 kHz and 64/128 kHz (Pulse only) are Unsupported here.
inline RateClass rateClass(int hz) {
    switch (hz) {
        case 44100: case 48000: return RateClass::Single;
        case 88200: case 96000: return RateClass::Double;
        case 176400: case 192000: return RateClass::Quad;
        default: return RateClass::Unsupported;
    }
}
constexpr int kDigifacePorts = 4;
inline int adatPerPort(RateClass c) { return c == RateClass::Single ? 8 : c == RateClass::Double ? 4 : c == RateClass::Quad ? 2 : 0; }
// ADAT channels in each direction at this rate with all four ports used as ADAT: 32, 16 or 8.
inline int adatChannels(int hz) { return adatPerPort(rateClass(hz)) * kDigifacePorts; }
// Hardware channel numbering used by the Digiface backend: ADAT 0..31, phones 32/33 (outputs only).
inline bool isPhones(int hw) { return hw >= 32; }
// ASSUMPTION: the first adatChannels(hz) hardware channels stay active, the rest are unavailable.
inline bool hwActive(int hw, int hz) { return hw >= 0 && (isPhones(hw) ? rateClass(hz) != RateClass::Unsupported : hw < adatChannels(hz)); }

inline std::string hwName(int hw) {
    if (hw < 0) return "Software";
    if (isPhones(hw)) return hw == 32 ? "Phones L" : hw == 33 ? "Phones R" : "Ch " + std::to_string(hw + 1);
    return "ADAT " + std::to_string(hw + 1);
}
// Names for the channels of the ALSA device (node.channel-names): ADAT n for the first adatChannels(hz), "Ch n" beyond.
// How the kernel orders extra channels (SPDIF, phones) is UNKNOWN, so they are not named.
inline std::vector<std::string> channelNames(int total, int hz) {
    std::vector<std::string> v; const int a = adatChannels(hz);
    for (int i = 0; i < total; ++i) v.push_back(i < a ? "ADAT " + std::to_string(i + 1) : "Ch " + std::to_string(i + 1));
    return v;
}

// Pulse side: the unit lists its ADAT inputs in blocks of 8 (ADAT IN 1-8 ... 25-32, see docs/PULSE16-PROTOCOL.md).
// With a straight cable (Digiface port n to Pulse ADAT port n) at 44.1/48 kHz Digiface ADAT channel k reaches the same-numbered
// Pulse ADAT channel. At 88.2 kHz and above how the Pulse numbers S/MUX channels is UNKNOWN, so this returns -1.
inline int pulseAdatChannel(int hw, int hz) {
    return (rateClass(hz) == RateClass::Single && hw >= 0 && hw < 32) ? hw : -1;
}
// Index of the Pulse source block (the `col` of command 11) that holds this ADAT channel, 2 = ADAT IN 1-8. -1 if unknown.
inline int pulseSourceBlock(int hw, int hz) { const int c = pulseAdatChannel(hw, hz); return c < 0 ? -1 : 2 + c / 8; }

// ---- clock ----------------------------------------------------------------------------------------------------------
enum class ClockMaster { Digiface = 0, Pulse = 1 };
struct ClockPlan {
    bool ok = false;
    std::string note;                 // plain-words advice or the reason it is not ok
    bool sendPulseClock = false;      // write the Pulse clock source (command 01 01, CONFIRMED in the Pulse notes)
    wm::pulse16::Clock pulseClock = wm::pulse16::Clock::Adat;
    bool sendPulseRate = false;       // write the Pulse master rate (command 01 02); only valid when the Pulse is master
    wm::pulse16::Rate pulseRate = wm::pulse16::Rate::k48;
};
inline bool pulseRateFor(int hz, wm::pulse16::Rate& r) {
    using R = wm::pulse16::Rate;
    switch (hz) { case 44100: r = R::k44_1; return true; case 48000: r = R::k48; return true; case 88200: r = R::k88_2; return true;
                  case 96000: r = R::k96; return true; case 176400: r = R::k176_4; return true; case 192000: r = R::k192; return true; }
    return false;
}
// Only one device may be master. Digiface leads: the audio system picks the rate, the Pulse follows ADAT.
// Pulse leads: the Pulse generates the rate and the audio system must be opened at the same rate (the Digiface follows its ADAT input;
// WhiskerMix cannot set the Digiface clock source from Linux, that part is UNTESTED and may need the vendor mixer once).
inline ClockPlan planClock(ClockMaster m, int hz) {
    ClockPlan p; wm::pulse16::Rate r;
    if (rateClass(hz) == RateClass::Unsupported || !pulseRateFor(hz, r)) { p.note = "Sample rate not supported by both units (44.1 to 192 kHz)."; return p; }
    p.ok = true; p.sendPulseClock = true;
    if (m == ClockMaster::Digiface) {
        p.pulseClock = wm::pulse16::Clock::Adat;
        p.note = "Digiface is master at the audio system rate; the Pulse follows ADAT. The Pulse shows no lock until the Digiface sends ADAT.";
    } else {
        p.pulseClock = wm::pulse16::Clock::Master; p.sendPulseRate = true; p.pulseRate = r;
        p.note = "Pulse is master at the chosen rate; open the audio system at the same rate and set the Digiface to follow its ADAT input.";
    }
    return p;
}

// ---- PipeWire links --------------------------------------------------------------------------------------------------
inline std::string shellQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o.push_back(c); }
    return o + "'";
}
inline std::string pwLink(const std::string& outPort, const std::string& inPort) { return "pw-link " + shellQuote(outPort) + " " + shellQuote(inPort); }
inline std::string pwUnlink(const std::string& outPort, const std::string& inPort) { return "pw-link -d " + shellQuote(outPort) + " " + shellQuote(inPort); }

// A WhiskerMix output bus bound to Digiface playback channels (ADAT 1-based by hw index). `leftPort`/`rightPort` are real WhiskerMix port names.
struct BusLink { std::string leftPort, rightPort; int hwFirst = -1; bool stereo = true; };
// Commands that link each bus to its Digiface playback channels at this rate. Channels not active at the rate are skipped and reported.
// `devicePortPrefix` is the Digiface playback port name without the number, e.g. "alsa_output.usb-RME_Digiface_USB-00.pro-output-0:playback_AUX".
// The real names must be read from `pw-link -i` on the machine (not verified here); the AUX index is the 0-based ALSA channel.
inline std::vector<std::string> busLinkCommands(const std::vector<BusLink>& buses, const std::string& devicePortPrefix, int hz, std::vector<std::string>* skipped = nullptr) {
    std::vector<std::string> cmds;
    for (const auto& b : buses) {
        if (b.hwFirst < 0 || isPhones(b.hwFirst)) continue;   // software only, or phones (ALSA position unknown)
        const int n = b.stereo ? 2 : 1;
        for (int k = 0; k < n; ++k) {
            const int hw = b.hwFirst + k;
            if (!hwActive(hw, hz)) { if (skipped) skipped->push_back(b.leftPort + " -> " + hwName(hw) + " not active at " + std::to_string(hz) + " Hz"); continue; }
            cmds.push_back(pwLink(k == 0 ? b.leftPort : b.rightPort, devicePortPrefix + std::to_string(hw)));
        }
    }
    return cmds;
}

// ---- saved choices ---------------------------------------------------------------------------------------------------
// Clock master and rate are stored in the app settings as options (floats): "clkMaster" 0/1 and "hwRate" in Hz. Reading is tolerant.
inline ClockMaster masterFromOpt(float v) { return v > 0.5f ? ClockMaster::Pulse : ClockMaster::Digiface; }
inline int rateFromOpt(float v, int fallback = 48000) { const int hz = int(v + 0.5f); return rateClass(hz) == RateClass::Unsupported ? fallback : hz; }

// Strips of a mixer state whose hardware channel is not active at this rate (shown as unavailable, never deleted).
template <class StateT>
inline std::vector<std::string> unavailableStrips(const StateT& st, int hz) {
    std::vector<std::string> v;
    for (const auto& c : st.channels) {
        if (c.hwFirst < 0 || c.playback) continue;   // playback strips are software feeds into the device, checked via buses
        const int last = c.hwFirst + (c.stereoLinked ? 1 : 0);
        if (!hwActive(c.hwFirst, hz) || !hwActive(last, hz)) v.push_back(c.name);
    }
    for (const auto& b : st.buses) {
        if (b.hwFirst < 0) continue;
        const int last = b.hwFirst + (b.stereo ? 1 : 0);
        if (!hwActive(b.hwFirst, hz) || !hwActive(last, hz)) v.push_back(b.name);
    }
    return v;
}

}  // namespace wm::routing
