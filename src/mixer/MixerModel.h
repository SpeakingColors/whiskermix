// Mixer model: channels, buses, snapshots. Pure C++, no hardware or UI calls.
#pragma once
#include <string>
#include <vector>
#include <map>
#include "../backends/digiface/DigifaceCodec.h"
#include "../engine/Dsp.h"
#include "../engine/Reverb.h"

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
    bool hidden = false;        // hidden in the mixer (Settings > Channel layout)
    int hwFirst = -1;           // Digiface hardware channel of this strip (0-based; stereo strips use hwFirst and hwFirst+1); -1 = not on the hardware
    bool playback = false;      // software playback strip (true) or hardware input (false)
    wm::engine::FxParams fx;
};

struct Bus {
    std::string name;
    float volumeDb = 0.0f;
    bool dim = false;
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
    const State& state() const { return state_; }

    int addChannel() {
        state_.channels.emplace_back();
        state_.channels.back().name = "In " + std::to_string(state_.channels.size());
        return int(state_.channels.size()) - 1;
    }
    void removeLastChannel() { if (!state_.channels.empty()) state_.channels.pop_back(); }
    void rename(int ch, const std::string& n) { state_.channels.at(size_t(ch)).name = n; }

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
        return digiface::mainVolume(bank, b.volumeDb + (b.dim ? digiface::kDimDb : 0.0f));
    }

private:
    State state_;
    std::map<std::string, State> snaps_;
};

}  // namespace wm::mixer
