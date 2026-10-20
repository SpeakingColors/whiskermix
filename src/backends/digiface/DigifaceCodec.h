// Digiface USB mixer codec: gain values and packet layout. Pure C++, no hardware calls.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace wm::digiface {

constexpr uint16_t kGainTop = 0xA000;    // +6.02 dB
constexpr uint16_t kGainUnity = 0x9000;  // 0 dB
constexpr uint16_t kPhaseInvert = 0xF000;

// One 32-bit little-endian word on the bulk OUT endpoint.
struct Word {
    uint16_t value;
    uint8_t channel;
    uint8_t flag;
    void pack(std::vector<uint8_t>& out) const {
        out.push_back(value & 0xFF);
        out.push_back(value >> 8);
        out.push_back(channel);
        out.push_back(flag);
    }
};

// One vendor control write (bmRequestType 0x40, bRequest 0x15).
struct ControlWrite {
    uint16_t value;
    uint16_t index;
};

// Gain value: linear amplitude in two ranges.
//   value <  0x8000: amplitude = value / 32768
//   value >= 0x8000: amplitude = (value - 0x8000) / 4096
// Truncated, not rounded: the vendor mixer writes 0x8B53 for a typed -3 dB (rounding would give 0x8B54).
inline uint16_t encodeGain(float db) {
    if (db <= -120.0f) return 0;
    const double amp = std::min(2.0, std::pow(10.0, double(db) / 20.0));
    if (amp < 0.5) return uint16_t(amp * 32768.0);
    const long v = 0x8000 + long(amp * 4096.0);
    return uint16_t(std::min<long>(v, kGainTop));
}

inline float decodeGain(uint16_t v) {
    if (v == 0) return -120.0f;
    const double amp = v < 0x8000 ? v / 32768.0 : (v - 0x8000) / 4096.0;
    return amp <= 0.0 ? -120.0f : float(20.0 * std::log10(amp));
}

// Slot table (CONFIRMED by the "all faders" capture, 2026-10-20). The device keeps 16 handles 0x40..0x4F. A flag word
// (flag 0x40) binds a handle to a signal path: value = (reg << 9) | (playback << 8) | hardwareChannel, where
// reg 0 and 2 feed the left output of the submix, reg 1 and 3 the right (2 and 3 are a second register set that is
// always written with the same gain; its role is UNKNOWN). Gain words (flag 0) then set the gain of the bound path.
// The vendor mixer uses handles 0x48..0x4B for the input strip being moved and 0x40..0x43 (or 0x44..0x47) for playback
// strips, binding the four paths at the start of every gesture: left input -> reg 2 and 0, right input -> reg 3 and 1
// (a mono strip binds the same channel to all four).
constexpr uint8_t kInputSlot = 0x48, kPlaybackSlot = 0x40;
inline uint16_t bindValue(int reg, bool playback, int hwChannel) {
    return uint16_t(((reg & 3) << 9) | ((playback ? 1 : 0) << 8) | (hwChannel & 0xFF));
}
// Slot order within a strip block: slot+0 = reg 2 (L), slot+1 = reg 3 (R), slot+2 = reg 0 (L), slot+3 = reg 1 (R).
constexpr int kSlotReg[4] = {2, 3, 0, 1};
inline bool slotIsRight(int i) { return (kSlotReg[i] & 1) != 0; }

// One strip write as the vendor mixer sends it: four bind words and four gains (left/right output paths).
// gainL / gainR already include pan; phaseL / phaseR replace the gain with the phase-invert marker.
inline std::vector<Word> stripBlock(uint8_t slot, bool playback, int hwFirst, bool stereo, uint16_t gainL, uint16_t gainR,
                                    bool phaseL = false, bool phaseR = false) {
    std::vector<Word> w;
    for (int i = 0; i < 4; ++i) {
        const bool right = slotIsRight(i);
        const int hw = hwFirst + ((stereo && right) ? 1 : 0);
        const uint16_t g = right ? (phaseR ? kPhaseInvert : gainR) : (phaseL ? kPhaseInvert : gainL);
        w.push_back({g, uint8_t(slot + i), 0x00});
        w.push_back({bindValue(kSlotReg[i], playback, hw), uint8_t(slot + i), 0x40});
    }
    return w;
}
// Gain-only update on an already bound block (what the vendor mixer sends while a fader keeps moving).
inline std::vector<Word> stripGains(uint8_t slot, uint16_t gainL, uint16_t gainR, bool phaseL = false, bool phaseR = false) {
    std::vector<Word> w;
    for (int i = 0; i < 4; ++i) {
        const bool right = slotIsRight(i);
        w.push_back({right ? (phaseR ? kPhaseInvert : gainR) : (phaseL ? kPhaseInvert : gainL), uint8_t(slot + i), 0x00});
    }
    return w;
}
// Pan without pan law: the far side is attenuated, the near side keeps the fader gain. pan in [-1, 1].
inline void panGains(float db, float pan, uint16_t& left, uint16_t& right) {
    const float side = std::abs(std::clamp(pan, -1.0f, 1.0f));
    const float farDb = side <= 0.0f ? db : (side >= 1.0f ? -120.0f : db + 20.0f * std::log10(1.0f - side));
    left = encodeGain(pan > 0.0f ? farDb : db);     // panning right attenuates the left output
    right = encodeGain(pan < 0.0f ? farDb : db);
}

// Older helpers kept for the tests of the first captures (input 1 and 2 on handles 0x48..0x4B).
inline uint8_t gainByte(int input, int side) { return uint8_t(0x48 + 2 * input + side); }
inline std::vector<Word> channelGain(int input, float db) {
    const uint16_t v = encodeGain(db);
    return {{v, gainByte(input, 0), 0}, {v, gainByte(input, 1), 0}};
}
inline std::vector<Word> channelGainPan(int input, float db, float pan) {
    uint16_t l, r; panGains(db, pan, l, r);
    return {{l, gainByte(input, 0), 0}, {r, gainByte(input, 1), 0}};
}
inline std::vector<Word> phase(int firstInput, int count, int side, bool invert, float db = 0.0f) {
    std::vector<Word> w;
    for (int i = 0; i < count; ++i)
        w.push_back({invert ? kPhaseInvert : encodeGain(db), gainByte(firstInput + i, side), 0});
    return w;
}

// Hardware output volume (CONFIRMED by the "all faders" capture): vendor control OUT, wIndex = 0x0100 + output channel
// (0-based hardware output). A stereo strip writes both channels with the same value. Main = outputs 0/1 (ADAT 1/2),
// Speaker B = outputs 2/3 in the captured setup.
inline std::vector<ControlWrite> outputVolume(int hwFirst, bool stereo, float db) {
    const uint16_t v = encodeGain(db);
    std::vector<ControlWrite> w{{v, uint16_t(0x0100 + hwFirst)}};
    if (stereo) w.push_back({v, uint16_t(0x0100 + hwFirst + 1)});
    return w;
}
// Older name: bank 0 = Main A (outputs 0/1), 1 = Main B (outputs 2/3).
inline std::vector<ControlWrite> mainVolume(int bank, float db) { return outputVolume(2 * bank, true, db); }

constexpr float kDimDb = -20.0f;

// Speaker switch: the inactive bank is set to zero, the active bank to its level.
inline std::vector<ControlWrite> speakerSelect(int activeBank, float levelA, float levelB) {
    auto a = mainVolume(0, activeBank == 0 ? levelA : -120.0f);
    auto b = mainVolume(1, activeBank == 1 ? levelB : -120.0f);
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

inline std::vector<uint8_t> pack(const std::vector<Word>& words) {
    std::vector<uint8_t> out;
    for (const auto& w : words) w.pack(out);
    return out;
}

}  // namespace wm::digiface
