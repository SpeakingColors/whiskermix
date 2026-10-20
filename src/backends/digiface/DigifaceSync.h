// Keeps the Digiface USB hardware mixer in step with the mixer model: call sync() whenever the state may have
// changed (a 20 ms timer in the app, like the vendor mixer's write pace). Only what changed is written, so a
// fader move costs one small transfer and an idle mixer costs nothing.
//
// What is written comes from the labeled USBPcap captures (docs/DIGIFACE-PROTOCOL.md):
//   input and playback strips -> one block per strip on the slot table: 4 bind words (hardware channel, playback
//                                flag, register) + 4 gains with pan and phase      (CONFIRMED, "all faders" capture)
//   mute, solo                -> gain 0 (the vendor mixer sends nothing on USB for them)
//   hardware outputs          -> control writes 0x0100 + output channel             (CONFIRMED, same capture)
//   dim                       -> Main at -20 dB; speaker B -> the other pair at 0    (CONFIRMED)
//   stereo link, M/S          -> the captured bursts, rebuilt for any pair           (first pair CONFIRMED, others EXTRAPOLATED)
//   main mono                 -> the captured 24-word / 16-word bursts (playbacks 1..4)
// Everything can run against a RecordingTransport (dry run).
#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "DigifaceTransport.h"
#include "../../mixer/MixerModel.h"

namespace wm::digiface {

// Stereo link: the two input channels keep their straight paths at -3 dB and get cross paths (L -> right out,
// R -> left out) at -3 dB on the next four handles. Captured for inputs 1+2 on handles 0x48..0x4F.
inline std::vector<Word> stereoOn(int hwL, int hwR, bool playback = false, uint8_t slot = 0x48) {
    std::vector<Word> w;
    for (int i = 0; i < 8; ++i) w.push_back({0x8B50, uint8_t(slot + i), 0x00});
    // straight paths (slot+0..3): L -> reg 2, R -> reg 3, L -> reg 0, R -> reg 1; cross paths (slot+4..7): L -> reg 3, R -> reg 2, L -> reg 1, R -> reg 0
    const int reg[8] = {2, 3, 0, 1, 3, 2, 1, 0};
    for (int i = 0; i < 8; ++i) w.push_back({bindValue(reg[i], playback, (i % 2) ? hwR : hwL), uint8_t(slot + i), 0x40});
    return w;
}
// M/S processing: mid at -6 dB on the straight and cross paths, side at 0x4000 (its own marker, not decoded).
inline std::vector<Word> midSideOn(int hwL, int hwR, bool playback = false, uint8_t slot = 0x48) {
    auto w = stereoOn(hwL, hwR, playback, slot);
    for (int i = 0; i < 8; ++i) w[size_t(i)].value = (i == 1 || i == 3) ? 0x4000 : 0x8800;
    return w;
}
// Stereo off and M/S off: straight paths back to 0 dB, cross paths to 0.
inline std::vector<Word> pairOff(uint8_t slot = 0x48) {
    std::vector<Word> w;
    for (int i = 0; i < 4; ++i) w.push_back({0x9000, uint8_t(slot + i), 0});
    for (int i = 4; i < 8; ++i) w.push_back({0x0000, uint8_t(slot + i), 0});
    return w;
}
inline std::vector<Word> mainMonoOn() {
    std::vector<Word> w;
    for (int b = 0x40; b <= 0x4F; ++b) w.push_back({0x8800, uint8_t(b), 0});
    const uint16_t f[8] = {0x0700, 0x0501, 0x0702, 0x0503, 0x0300, 0x0101, 0x0302, 0x0103};
    for (int i = 0; i < 8; ++i) w.push_back({f[i], uint8_t(0x48 + i), 0x40});
    return w;
}
inline std::vector<Word> mainMonoOff() {
    std::vector<Word> w;
    for (int b = 0x40; b <= 0x47; ++b) w.push_back({0x9000, uint8_t(b), 0});
    for (int b = 0x48; b <= 0x4F; ++b) w.push_back({0x0000, uint8_t(b), 0});
    return w;
}

class Sync {
public:
    explicit Sync(Transport& t) : tr_(&t) {}
    // Switch to another transport (the real device after a reconnect); everything is sent again on the next sync().
    void setTransport(Transport& t) { tr_ = &t; invalidate(); }

    // Mixer buses used as Main (control room) and as Speaker B; -1 = none. Only one of the two plays at a time.
    void setMainBuses(int mainA, int mainB) { busA_ = mainA; busB_ = mainB; }
    void setSpeakerB(bool on) { speakerB_ = on; }
    void setMainMono(bool on) { mono_ = on; }

    // Forget what was sent so the next sync() rewrites everything (after the device reconnects).
    void invalidate() { strips_.clear(); ctl_.clear(); pairMode_.clear(); }

    // Returns the number of USB transfers made.
    int sync(const wm::mixer::Mixer& m) {
        int n = 0;
        const auto& st = m.state();
        // 1. stereo link / M/S bursts per strip, only when the mode changes after the first sync (what the device
        //    has at start is unknown, so the first sync records the mode and sends no burst)
        for (size_t c = 0; c < st.channels.size(); ++c) {
            const auto& a = st.channels[c];
            if (a.hwFirst < 0) continue;
            const int mode = a.midSide ? 2 : (a.stereoLinked ? 1 : 0);
            auto it = pairMode_.find(key(a));
            if (it == pairMode_.end()) { pairMode_[key(a)] = mode; continue; }
            if (it->second == mode) continue;
            if (mode == 2) send(midSideOn(a.hwFirst, a.hwFirst + 1, a.playback, slotOf(a)));
            else if (mode == 1) send(stereoOn(a.hwFirst, a.hwFirst + 1, a.playback, slotOf(a)));
            else send(pairOff(slotOf(a)));
            ++n; it->second = mode;
            strips_.erase(key(a));   // the burst changed the gains: the strip block is sent again below
        }
        // 2. main mono (before the strips: the captured OFF burst zeroes handles 0x48..0x4F, so those strips are sent again)
        if (int(mono_) != monoSent_) {
            send(mono_ ? mainMonoOn() : mainMonoOff()); ++n;
            monoSent_ = int(mono_);
            strips_.clear();
        }
        // 3. strips: fader, pan, phase, mute, solo -> one block per changed strip
        std::vector<Word> words;
        for (size_t c = 0; c < st.channels.size(); ++c) {
            const auto& ch = st.channels[c];
            if (ch.hwFirst < 0) continue;
            uint16_t l, r; panGains(m.effectiveGainDb(int(c)), ch.pan, l, r);
            const Desired d{ch.hwFirst, ch.stereoLinked, ch.playback, l, r, ch.phaseL, ch.phaseR};
            auto it = strips_.find(key(ch));
            if (it != strips_.end() && it->second == d) continue;
            auto b = stripBlock(slotOf(ch), ch.playback, ch.hwFirst, ch.stereoLinked, l, r, ch.phaseL, ch.phaseR);
            words.insert(words.end(), b.begin(), b.end());
            strips_[key(ch)] = d;
        }
        if (!words.empty()) { send(words); ++n; }
        // 4. hardware outputs: volume with dim on Main; Main and Speaker B: the inactive one is written as 0
        for (size_t b = 0; b < st.buses.size(); ++b) {
            const auto& bus = st.buses[b];
            if (bus.hwFirst < 0) continue;
            float db = bus.volumeDb + (bus.dim ? kDimDb : 0.0f);
            if (int(b) == busA_ && busB_ >= 0 && speakerB_) db = -120.0f;
            if (int(b) == busB_ && !speakerB_) db = -120.0f;
            for (const auto& x : outputVolume(bus.hwFirst, bus.stereo, db)) {
                auto it = ctl_.find(x.index);
                if (it != ctl_.end() && it->second == x.value) continue;
                if (tr_->controlOut(x)) ++n;
                ctl_[x.index] = x.value;
            }
        }
        return n;
    }

    // Last gains written per strip (hardware channel key), for tests and the activity log.
    struct Desired { int hw; bool stereo, playback; uint16_t l, r; bool phaseL, phaseR;
        bool operator==(const Desired& o) const { return hw == o.hw && stereo == o.stereo && playback == o.playback && l == o.l && r == o.r && phaseL == o.phaseL && phaseR == o.phaseR; } };
    const std::map<int, Desired>& written() const { return strips_; }
    const std::map<uint16_t, int>& outputs() const { return ctl_; }

private:
    static int key(const wm::mixer::Channel& c) { return (c.playback ? 1000 : 0) + c.hwFirst; }
    static uint8_t slotOf(const wm::mixer::Channel& c) { return c.playback ? kPlaybackSlot : kInputSlot; }
    void send(const std::vector<Word>& w) { tr_->bulkOut(pack(w)); }
    Transport* tr_;
    int busA_ = -1, busB_ = -1;
    bool speakerB_ = false, mono_ = false;
    int monoSent_ = 0;   // mono is assumed off at start; nothing is sent until it is switched
    std::map<int, Desired> strips_;
    std::map<uint16_t, int> ctl_;
    std::map<int, int> pairMode_;
};

}  // namespace wm::digiface
