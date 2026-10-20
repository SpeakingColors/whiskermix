// Pulse 16 session model: connect handshake, live state, and safe setters. Pure C++, no JUCE.
// The MIDI port is hidden behind Transport so tests (and a dry-run mode) need no hardware.
#pragma once
#include <functional>
#include "Pulse16Codec.h"

namespace wm::pulse16 {

struct Transport {
    virtual ~Transport() = default;
    virtual void sendSysEx(const Bytes& msg) = 0;
};

class Session {
public:
    explicit Session(Transport& t) : tr_(t) {}

    // Same opening as RemoteFish: probe, then 02 01. The unit answers with its whole state
    // (gains, routing, settings blocks). We do NOT send 01 04 01 (it would force headphone source Analog Out).
    void connect() { connected_ = false; tr_.sendSysEx(probe()); tr_.sendSysEx(hello()); }

    // Feed every complete SysEx message received from the Pulse.
    void onSysEx(const Bytes& m) {
        if (m.size() == 12 && m[9] == 0x03 && m[10] == 0x00) { connected_ = true; return; } // probe answer (carries the id bytes)
        if (m.size() < 12) return;
        if (parseGains(m, st_)) { haveGains_ = true; return; }
        if (parseRouting(m, st_)) { haveRouting_ = true; return; }
        if (m.size() == 13 && m[9] == 0x01) {          // echo of a setting we (or the front panel) changed
            if (m[10] == 0x01 && m[11] <= 2) { st_.clock = Clock(m[11]); st_.clockKnown = true; }
            else if (m[10] == 0x02 && m[11] <= 8) { st_.rate = Rate(m[11]); st_.rateKnown = true; }
        }
    }

    bool connected() const { return connected_; }
    bool haveState() const { return haveGains_ && haveRouting_; }
    const Pulse16State& state() const { return st_; }

    // Setters send the command and update the local model. Values are clamped by the codec.
    void setInputGain(int ch, int dB) { tr_.sendSysEx(inputGain(ch, dB)); st_.inputGainDb[ch - 1] = std::clamp(dB, 0, 28); }
    void setOutputLevel(int ch, int dB) { tr_.sendSysEx(outputLevel(ch, dB)); st_.outputLevelDb[ch - 1] = std::clamp(dB, -28, 0); }
    void setRoute(int row, int col) { tr_.sendSysEx(route(row, col)); if (row >= 0 && row < 22) st_.routeCol[row] = uint8_t(col); }
    void setClock(Clock c) { tr_.sendSysEx(clockSource(c)); st_.clock = c; st_.clockKnown = true; }
    // The Pulse screen can show 48 kHz while the unit runs another rate, and re-picking the shown value sends nothing in
    // RemoteFish. So always send the rate explicitly, even when the model already says it.
    void setRate(Rate r) { tr_.sendSysEx(sampleRate(r)); st_.rate = r; st_.rateKnown = true; }
    void savePresetSlot(int n) { tr_.sendSysEx(savePreset(n)); }   // overwrites the stored slot
    void loadPresetSlot(int n) { tr_.sendSysEx(loadPreset(n)); haveGains_ = haveRouting_ = false; } // unit replies with new state

private:
    Transport& tr_;
    Pulse16State st_;
    bool connected_ = false, haveGains_ = false, haveRouting_ = false;
};

} // namespace wm::pulse16
