// Mixer model: channels, buses, snapshots. Pure C++, no hardware or UI calls.
#pragma once
#include <string>
#include <vector>
#include <map>
#include "../backends/digiface/DigifaceCodec.h"
#include "../engine/Dsp.h"

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
    wm::engine::FxParams fx;
};

struct Bus {
    std::string name;
    float volumeDb = 0.0f;
    bool dim = false;
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
