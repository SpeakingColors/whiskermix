// Keeps the Digiface USB hardware mixer in step with the mixer model: call sync() whenever the state may have
// changed (a 20 ms timer in the app, like the vendor mixer's write pace). Only what changed is written, so a
// fader move costs two words and an idle mixer costs nothing.
//
// What is written comes straight from the labeled USBPcap captures (docs/DIGIFACE-PROTOCOL.md):
//   channel gain/pan/phase  -> bulk words 0x48 + 2*input + side          (confirmed for inputs 1 and 2)
//   mute, solo              -> gain 0 (the vendor mixer sends nothing on USB for them)
//   stereo link, M/S        -> the captured 12-word / 8-word bursts     (confirmed for the first pair only)
//   main volume, dim        -> control writes 0x0100/0x0101 (A), 0x0102/0x0103 (B)
//   speaker B               -> speakerSelect()
//   main mono               -> the captured 24-word / 16-word bursts
// Inputs beyond the second and other submixes use the same byte layout, which is an extrapolation until a
// capture confirms it (see the protocol doc). Everything can run against a RecordingTransport (dry run).
#pragma once
#include <cstdint>
#include <map>
#include <vector>
#include "DigifaceTransport.h"
#include "../../mixer/MixerModel.h"

namespace wm::digiface {

// Captured bursts for the first stereo pair (inputs 1 and 2, bytes 0x48..0x4F). `base` shifts them to another pair
// (extrapolated: base = 0x48 + 8 * pair).
inline std::vector<Word> stereoOn(uint8_t base = 0x48) {
    std::vector<Word> w;
    for (int i = 0; i < 8; ++i) w.push_back({0x8B50, uint8_t(base + i), 0x00});
    w.push_back({0x0600, uint8_t(base + 4), 0x40}); w.push_back({0x0401, uint8_t(base + 5), 0x40});
    w.push_back({0x0200, uint8_t(base + 6), 0x40}); w.push_back({0x0001, uint8_t(base + 7), 0x40});
    return w;
}
inline std::vector<Word> midSideOn(uint8_t base = 0x48) {
    std::vector<Word> w;
    w.push_back({0x8800, base, 0}); w.push_back({0x4000, uint8_t(base + 1), 0});
    w.push_back({0x8800, uint8_t(base + 2), 0}); w.push_back({0x4000, uint8_t(base + 3), 0});
    for (int i = 4; i < 8; ++i) w.push_back({0x8800, uint8_t(base + i), 0});
    w.push_back({0x0600, uint8_t(base + 4), 0x40}); w.push_back({0x0401, uint8_t(base + 5), 0x40});
    w.push_back({0x0200, uint8_t(base + 6), 0x40}); w.push_back({0x0001, uint8_t(base + 7), 0x40});
    return w;
}
// Stereo off and M/S off are the same 8-word burst.
inline std::vector<Word> pairOff(uint8_t base = 0x48) {
    std::vector<Word> w;
    for (int i = 0; i < 4; ++i) w.push_back({0x9000, uint8_t(base + i), 0});
    for (int i = 4; i < 8; ++i) w.push_back({0x0000, uint8_t(base + i), 0});
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

    // Which hardware inputs the mixer channels map to: channel i -> hardware input `firstInput + i`.
    // Channels past `inputCount` (software playback) are not written: their bytes are not confirmed yet.
    void setInputRange(int firstInput, int inputCount) { first_ = firstInput; count_ = inputCount; }
    // Mixer bus used as Main A (control room) and as Main B (speaker B); -1 = none.
    void setMainBuses(int mainA, int mainB) { busA_ = mainA; busB_ = mainB; }
    void setSpeakerB(bool on) { speakerB_ = on; }
    void setMainMono(bool on) { mono_ = on; }

    // Forget what was sent so the next sync() rewrites everything (after the device reconnects).
    void invalidate() { sent_.clear(); ctl_.clear(); linked_ = ms_ = 0; }

    // Returns the number of USB transfers made.
    int sync(const wm::mixer::Mixer& m) {
        int n = 0;
        const auto& st = m.state();
        std::vector<Word> words;
        // 1. pair modes (stereo link, M/S) for the first pair only: those bursts were captured. For other pairs a
        //    link is handled in software (both channels get the same gain) until a capture shows their burst.
        if (count_ >= 2 && st.channels.size() >= 2) {
            const auto& a = st.channels[0];
            const int link = a.stereoLinked ? 1 : 0, ms = a.midSide ? 1 : 0;
            if (ms != ms_ || link != linked_) {
                std::vector<Word> b;
                if (ms) b = midSideOn();
                else if (link) b = stereoOn();
                else b = pairOff();
                if (tr_->bulkOut(pack(b))) ++n;
                ms_ = ms; linked_ = link;
                for (const auto& w : b) if (w.flag == 0) sent_[w.channel] = w.value;   // the burst set these gains
            }
        }
        // 2. main mono (before the gains: the captured OFF burst zeroes bytes 0x48..0x4F, so those gains are sent again below)
        if (int(mono_) != monoSent_) {
            if (tr_->bulkOut(pack(mono_ ? mainMonoOn() : mainMonoOff()))) ++n;
            monoSent_ = int(mono_);
            for (int b = 0x48; b <= 0x4F; ++b) sent_.erase(uint8_t(b));
        }
        // 3. channel gains: fader, pan, phase, mute, solo
        for (int c = 0; c < count_ && c < int(st.channels.size()); ++c) {
            auto w = m.digifaceWords(c);
            for (auto& x : w) {
                x.channel = uint8_t(0x48 + 2 * (first_ + c) + (x.channel & 1));
                if (val(sent_, x.channel, -1) != int(x.value)) { words.push_back(x); sent_[x.channel] = x.value; }
            }
        }
        if (!words.empty() && tr_->bulkOut(pack(words))) ++n;
        // 4. main outputs: volume with dim, speaker B switch
        if (busA_ >= 0 && busA_ < int(st.buses.size())) {
            const float a = level(st.buses[size_t(busA_)]);
            const float b = (busB_ >= 0 && busB_ < int(st.buses.size())) ? level(st.buses[size_t(busB_)]) : -120.0f;
            std::vector<ControlWrite> cw = busB_ >= 0 ? speakerSelect(speakerB_ ? 1 : 0, a, b) : mainVolume(0, a);
            for (const auto& x : cw)
                if (val(ctl_, x.index, -1) != int(x.value)) { if (tr_->controlOut(x)) ++n; ctl_[x.index] = x.value; }
        }
        return n;
    }

    // Last value written per byte (tests and the SysEx-style log in the UI).
    const std::map<uint8_t, int>& written() const { return sent_; }

private:
    static float level(const wm::mixer::Bus& b) { return b.volumeDb + (b.dim ? kDimDb : 0.0f); }
    template <class M, class K> static int val(const M& m, K k, int def = 0) { auto it = m.find(k); return it == m.end() ? def : it->second; }
    Transport* tr_;
    int first_ = 0, count_ = 0, busA_ = -1, busB_ = -1;
    bool speakerB_ = false, mono_ = false;
    int monoSent_ = 0;   // mono is assumed off at start; nothing is sent until it is switched
    std::map<uint8_t, int> sent_;
    std::map<uint16_t, int> ctl_;
    int linked_ = 0, ms_ = 0;   // assumed off at start, like mono
};

}  // namespace wm::digiface
