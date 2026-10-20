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

// Gain from input `input` (0-based) to output side `side` (0 = left, 1 = right).
inline uint8_t gainByte(int input, int side) { return uint8_t(0x48 + 2 * input + side); }

// Channel fader: same value to both output sides.
inline std::vector<Word> channelGain(int input, float db) {
    const uint16_t v = encodeGain(db);
    return {{v, gainByte(input, 0), 0}, {v, gainByte(input, 1), 0}};
}

// Pan without pan law: only the far side is attenuated. pan in [-1, 1].
inline std::vector<Word> channelGainPan(int input, float db, float pan) {
    const float side = std::abs(std::clamp(pan, -1.0f, 1.0f));
    const float farDb = side <= 0.0f ? db : (side >= 1.0f ? -120.0f : db + 20.0f * std::log10(1.0f - side));
    const bool right = pan > 0.0f;  // panning right attenuates the left output
    return {{encodeGain(right ? farDb : db), gainByte(input, 0), 0},
            {encodeGain(right ? db : farDb), gainByte(input, 1), 0}};
}

// Phase invert for one output side across the given inputs (uninverted value = given gain).
inline std::vector<Word> phase(int firstInput, int count, int side, bool invert, float db = 0.0f) {
    std::vector<Word> w;
    for (int i = 0; i < count; ++i)
        w.push_back({invert ? kPhaseInvert : encodeGain(db), gainByte(firstInput + i, side), 0});
    return w;
}

// Main output volume. bank 0 = Main A, 1 = Main B. Left and right always written together.
inline std::vector<ControlWrite> mainVolume(int bank, float db) {
    const uint16_t v = encodeGain(db);
    const uint16_t base = uint16_t(0x0100 + 2 * bank);
    return {{v, base}, {v, uint16_t(base + 1)}};
}

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
