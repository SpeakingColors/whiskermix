// Mixer model: channels, buses, snapshots. Pure C++, no hardware or UI calls.
#pragma once
#include <string>
#include <vector>
#include <map>
#include "../backends/digiface/DigifaceCodec.h"
#include "../engine/Dsp.h"
#include "../engine/Reverb.h"
#include "../engine/Chain.h"

namespace wm::mixer {

struct Channel {
    std::string name;
    float gainDb = 0.0f;
    float pan = 0.0f;        // -1 left .. +1 right
    bool mute = false;
    bool solo = false;
    bool phaseL = false;
    bool phaseR = false;
    bool stereoLinked = false;  // linked with the next channel
    int colour = 0;             // index into the 9 soda pop strip colours
    std::string emoji;          // shown next to the name; shared with the MIDI device card
    float width = 1.0f;         // stereo width 0..2 (1 = unchanged)
    bool midSide = false;       // M/S processing
    float fxSendDb = -60.0f;    // send to the FX bus; -60 = off
    bool cue = false;           // cue (headphone pre-listen)
    bool trim = false;          // T button: the fader moves all routings (flag only; the Digiface submix mapping is not decoded yet)
    bool hidden = false;        // hidden in the mixer (Settings > Channel layout)
    int hwFirst = -1;           // Digiface hardware channel of this strip (0-based; stereo strips use hwFirst and hwFirst+1); -1 = not on the hardware
    bool playback = false;      // software playback strip (true) or hardware input (false)
    wm::engine::FxParams fx;
    wm::engine::FxChainParams chain;   // Reverb, Echo, Chorus, Phaser, Flanger, M/S, MB Comp, Limiter, Purr Trio, Opto (each on/off + 4 knobs)
};

struct Bus {
    std::string name;
    float volumeDb = 0.0f;
    bool dim = false;
    float dimDb = -20.0f;       // how far Dim lowers the output (configurable); switching Dim off returns to the previous level
    bool mono = false;          // mono check: left and right summed (WhiskerMix's own output path)
    bool mute = false;
    float pan = 0.0f;           // output balance -1..+1 (WhiskerMix's own output path; the Digiface hardware write is not decoded yet)
    bool link = false;          // control room: moving one volume moves the linked ones by the same amount
    bool solo = false;          // control room: only this output plays (Speaker A/B switch); none set = the first one
    bool controlRoom = false;   // belongs to the control room (monitor outputs); the layout decides how many there are
    int crRole = 0;             // control room: 1 = Main out, 2 = Main out B (Speaker B), 3..6 = Phones 1-4, 7 = Cue/PFL to, 0 = other
    int hwFirst = -1;           // Digiface hardware output (0-based); -1 = software only
    bool stereo = true;
    // Bus effects (use a dedicated bus as an FX return): reverb/shimmer reverb and shimmer delay.
    bool reverbOn = false, delayOn = false;
    wm::engine::ReverbSettings reverb;
    wm::engine::ShimmerDelayParams delay;
};

// A snapshot is a full copy of the state, so recalling it is exact.
struct State {
    std::vector<Channel> channels;
    std::vector<Bus> buses;
};

class Mixer {
public:
    explicit Mixer(int channelCount = 2, int busCount = 1) {
        state_.channels.resize(size_t(channelCount));
        state_.buses.resize(size_t(busCount));
        for (int i = 0; i < channelCount; ++i) state_.channels[size_t(i)].name = "In " + std::to_string(i + 1);
        for (int i = 0; i < busCount; ++i) state_.buses[size_t(i)].name = "Bus " + std::to_string(i + 1);
    }
    State& state() { return state_; }
    void addBus(const std::string& name, int hwFirst, bool stereo) { Bus b; b.name = name; b.hwFirst = hwFirst; b.stereo = stereo; state_.buses.push_back(b); }
    const State& state() const { return state_; }

    int addChannel() {
        state_.channels.emplace_back();
        state_.channels.back().name = "In " + std::to_string(state_.channels.size());
        return int(state_.channels.size()) - 1;
    }
    void removeLastChannel() { if (!state_.channels.empty()) state_.channels.pop_back(); }
    void rename(int ch, const std::string& n) { state_.channels.at(size_t(ch)).name = n; }

    // TotalMix "Stereo" button. Split: a stereo strip becomes two mono strips (same settings, hardware channels
    // hwFirst and hwFirst + 1, names "<name> 1" and "<name> 2"). Join: a mono strip and the next strip (its hardware
    // neighbour) become one stereo strip again. Returns true when something changed.
    bool splitStereo(int ch) {
        auto& c = state_.channels.at(size_t(ch));
        if (!c.stereoLinked) return false;
        Channel r = c;
        c.stereoLinked = false; r.stereoLinked = false; r.midSide = false; c.midSide = false;
        const std::string base = c.name;
        c.name = base + " 1"; r.name = base + " 2";
        if (r.hwFirst >= 0) r.hwFirst = c.hwFirst + 1;
        r.phaseL = c.phaseR; r.phaseR = c.phaseR; c.phaseR = c.phaseL;
        state_.channels.insert(state_.channels.begin() + ch + 1, r);
        return true;
    }
    bool canJoin(int ch) const {
        if (ch < 0 || ch + 1 >= int(state_.channels.size())) return false;
        const auto& a = state_.channels[size_t(ch)]; const auto& b = state_.channels[size_t(ch + 1)];
        if (a.stereoLinked || b.stereoLinked || a.playback != b.playback) return false;
        if (a.hwFirst >= 0 && (b.hwFirst != a.hwFirst + 1 || (a.hwFirst % 2) != 0)) return false;   // only hardware pairs 1-2, 3-4, ... like TotalMix
        return true;
    }
    bool joinStereo(int ch) {
        if (!canJoin(ch)) return false;
        auto& a = state_.channels[size_t(ch)]; const auto& b = state_.channels[size_t(ch + 1)];
        a.stereoLinked = true; a.phaseR = b.phaseL;
        auto strip = [](std::string n) { if (n.size() > 2 && (n.compare(n.size() - 2, 2, " 1") == 0 || n.compare(n.size() - 2, 2, " 2") == 0)) n.erase(n.size() - 2); return n; };
        a.name = strip(a.name);
        state_.channels.erase(state_.channels.begin() + ch + 1);
        return true;
    }

    bool anySolo() const {
        for (const auto& c : state_.channels) if (c.solo) return true;
        return false;
    }
    // Effective gain after mute and solo (mute and solo are software, so they become gain 0).
    float effectiveGainDb(int ch) const {
        const auto& c = state_.channels.at(size_t(ch));
        if (c.mute || (anySolo() && !c.solo)) return -120.0f;
        return c.gainDb;
    }

    // Snapshots
    void store(const std::string& n) { snaps_[n] = state_; }
    bool recall(const std::string& n) {
        auto it = snaps_.find(n);
        if (it == snaps_.end()) return false;
        state_ = it->second;
        return true;
    }
    std::vector<std::string> snapshotNames() const {
        std::vector<std::string> v;
        for (const auto& kv : snaps_) v.push_back(kv.first);
        return v;
    }

    // Digiface writes for one channel (first two channels of the selected bus, as decoded so far).
    std::vector<digiface::Word> digifaceWords(int ch) const {
        const auto& c = state_.channels.at(size_t(ch));
        auto w = digiface::channelGainPan(ch, effectiveGainDb(ch), c.pan);
        if (c.phaseL) w[0].value = digiface::kPhaseInvert;
        if (c.phaseR) w[1].value = digiface::kPhaseInvert;
        return w;
    }
    // Main volume writes with dim applied (dim is -20 dB).
    std::vector<digiface::ControlWrite> mainWrites(int bus, int bank) const {
        const auto& b = state_.buses.at(size_t(bus));
        return digiface::mainVolume(bank, b.mute ? -120.0f : b.volumeDb + (b.dim ? b.dimDb : 0.0f));
    }

private:
    State state_;
    std::map<std::string, State> snaps_;
};

// Control room roles: 1 Main out, 2 Main out B, 3..6 Phones 1-4, 7 Cue/PFL to. Main out and Main out B play one at a time; the others are always on.
// Main out and Main out B (index into buses, -1 = none). Without explicit roles the first two control room buses are used.
template <class S> inline int crMainIndex(const S& st) {
    for (size_t i = 0; i < st.buses.size(); ++i) if (st.buses[i].controlRoom && st.buses[i].crRole == 1) return int(i);
    for (size_t i = 0; i < st.buses.size(); ++i) if (st.buses[i].controlRoom && st.buses[i].crRole == 0) return int(i);
    return -1;
}
template <class S> inline int crAltIndex(const S& st) {
    for (size_t i = 0; i < st.buses.size(); ++i) if (st.buses[i].controlRoom && st.buses[i].crRole == 2) return int(i);
    const int m = crMainIndex(st);
    for (size_t i = 0; i < st.buses.size(); ++i) if (st.buses[i].controlRoom && st.buses[i].crRole == 0 && int(i) != m) return int(i);
    return -1;
}

}  // namespace wm::mixer
