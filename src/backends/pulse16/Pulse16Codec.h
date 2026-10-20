// Ferrofish Pulse 16 DX SysEx codec: frame builders and state-block parsers. Pure C++, no hardware calls.
// Tested in tests/pulse16_test.cpp. Protocol notes: docs/PULSE16-PROTOCOL.md.
// Source: USBPcap captures of RemoteFish 2.1 (firmware v5.04, ADAT-only DX), Oct 2026.
// Every value here was seen on the wire; items marked PROBABLE or UNVERIFIED were not proven.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>
#include <algorithm>

namespace wm::pulse16 {

using Bytes = std::vector<uint8_t>;

// Bytes after F0 00 11 22. UNVERIFIED: taken from one unit; may be per device.
// Learn it from the unit's reply to the probe if it differs.
inline constexpr uint8_t kId[5] = {0x0B, 0x3C, 0x45, 0x4F, 0x0B};

inline Bytes frame(std::initializer_list<uint8_t> body) {
    Bytes f{0xF0, 0x00, 0x11, 0x22, kId[0], kId[1], kId[2], kId[3], kId[4]};
    f.insert(f.end(), body.begin(), body.end());
    f.push_back(0xF7);
    return f;
}

// Connect sequence seen from RemoteFish: probe (reply starts 03 00), then 02 01, then 01 04 01.
// WARNING: 01 04 01 also selects headphone source Analog Out, do not replay blindly.
inline Bytes probe() { return {0xF0,0x00,0x11,0x22,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0xF7}; }
inline Bytes hello() { return frame({0x02, 0x01}); }
inline Bytes meterPoll() { return frame({0x02, 0x04}); } // about every 0.22 s; reply 04 00 01 + 32 bytes (undecoded)

// Faders. ch is 1..16.
// Input gain: 0..28 dB. CONFIRMED.
inline Bytes inputGain(int ch, int dB) {
    return frame({0x10, uint8_t(ch - 1), uint8_t(std::clamp(dB, 0, 28))});
}
// Output level: dB <= 0, sent as 28 + dB (0x1C = 0 dB). CONFIRMED. Lowest value not verified here.
inline Bytes outputLevel(int ch, int dB) {
    return frame({0x10, uint8_t(0x0F + ch), uint8_t(std::clamp(28 + dB, 0, 28))});
}

// Routing matrix cell. row = output block, col = input block (0-based, screen order).
// col 0 ANALOG IN 1-8, 1 ANALOG IN 9-16, 2 ADAT IN 1-8, 3 ADAT 9-16, 4 ADAT 17-24, 5 ADAT 25-32.
// Rows and cols 4 and up are a guess (MADI not captured).
inline Bytes route(int row, int col) { return frame({0x11, uint8_t(row), uint8_t(col)}); }

// Headphone. CONFIRMED unless noted.
inline Bytes headphoneVolume(int vv) { return frame({0x01, 0x03, uint8_t(std::clamp(vv, 0, 79))}); } // 0 = Off, dB = vv - 79
inline Bytes headphoneVolumeDb(int dB) { return headphoneVolume(79 + dB); }                         // dB -78..0
inline Bytes headphoneMute(bool on) { return frame({0x05, 0x00, 0x06, uint8_t(on ? 1 : 0)}); }      // PROBABLE
enum class HpSource : uint8_t { AnalogIn, AnalogOut, AdatIn, AdatOut, MadiIn, MadiOut, DanteIn, DanteOut };
inline Bytes headphoneSource(HpSource s) { return frame({0x01, 0x04, uint8_t(s)}); }
// Channel, source Analog Out. Stereo pair starting at odd channel, mono single channel.
inline Bytes headphoneStereo(int firstCh) { return frame({0x01, 0x05, uint8_t(0x40 + (firstCh - 1) / 2)}); }
inline Bytes headphoneMono(int ch) { return frame({0x01, 0x05, uint8_t(ch - 1)}); }

// Clock sync source. CONFIRMED. Master = the Pulse's own clock. Follow ADAT only if the other device is master.
enum class Clock : uint8_t { Master = 0, Adat = 1, Bnc = 2 };
inline Bytes clockSource(Clock c) { return frame({0x01, 0x01, uint8_t(c)}); }

// Master sync frequency (sample rate when the Pulse is clock master). CONFIRMED by dropdown order.
// 00 32k, 01 44.1k, 02 48k, 03 64k, 04 88.2k, 05 96k, 06 128k, 07 176.4k, 08 192k.
// The GUI shows 48 kHz even when the unit runs another rate, and re-picking the shown value sends nothing,
// so WhiskerMix must send this explicitly on connect.
enum class Rate : uint8_t { k32, k44_1, k48, k64, k88_2, k96, k128, k176_4, k192 };
inline Bytes sampleRate(Rate r) { return frame({0x01, 0x02, uint8_t(r)}); }
// WARNING: on the ADAT-only DX, RemoteFish sent this same 01 02 command for its MIDI routing, MADI 96k frame,
// MADI short frame and BNC termination controls (their index went into the rate byte). Do NOT map those to 01 02.
// The real commands for them are unknown.

inline Bytes displayBrightness(int v) { return frame({0x01, 0x0E, uint8_t(std::clamp(v, 0, 15))}); } // 0 darkest, 15 brightest. CONFIRMED
inline Bytes keyClick(bool on) { return frame({0x01, 0x0D, uint8_t(on ? 1 : 0)}); }    // PROBABLE
// Language: 00 = English PROBABLE. Do not expose others until captured.
// Not implemented (command unknown): MIDI routing, MADI 96k frame, MADI short frame, BNC termination.
// Delay compensation: 01 10 00/01 PROBABLE only.

// Presets, slots 1..6. CONFIRMED (save 1 to 6 then load 1 to 6 capture).
// Save N = 02 07 (N-1), ack 34. Load N = 02 06 (N-1) 07, ack 35, then the unit sends its state:
// a 47 byte block, 50 00 (gains), 51 00 (routing), 12 00 and 13 00 blocks (not decoded).
// RemoteFish follows every Load with 01 04 01 (headphone source Analog Out).
// Headphone volume is not part of a preset. Save overwrites the stored slot.
inline Bytes savePreset(int n) { return frame({0x02, 0x07, uint8_t(std::clamp(n, 1, 6) - 1)}); }
inline Bytes loadPreset(int n) { return frame({0x02, 0x06, uint8_t(std::clamp(n, 1, 6) - 1), 0x07}); }

// ---- Helpers for the UI (kept here so they are tested without JUCE) ----
// RemoteFish labels input gain as a reference level: dBu = 20 - gain (0 dB gain = +20 dBu, 28 dB gain = -8 dBu).
// Outputs: dBu = 20 + level dB (level 0 dB = +20 dBu).
inline int inputGainToDbu(int gainDb) { return 20 - gainDb; }
inline int dbuToInputGain(int dbu) { return std::clamp(20 - dbu, 0, 28); }
inline int outputLevelToDbu(int levelDb) { return 20 + levelDb; }
inline int dbuToOutputLevel(int dbu) { return std::clamp(dbu - 20, -28, 0); }
// SysEx framing: JUCE hands over the bytes between F0 and F7.
inline Bytes withFraming(const uint8_t* data, int n) { Bytes b{0xF0}; b.insert(b.end(), data, data + n); b.push_back(0xF7); return b; }
// Channel code of the headphone selector as shown in the UI. Returns false for codes outside the captured range.
inline bool decodeHeadphoneChannel(uint8_t code, int& firstCh, bool& stereo) {
    if (code >= 0x40 && code <= 0x50) { firstCh = (code - 0x40) * 2 + 1; stereo = true; return true; }   // 1+2 .. 33+34
    if (code <= 0x21) { firstCh = code + 1; stereo = false; return true; }                              // mono 1..34
    return false;
}
inline const char* kInputBlockNames[8] = {"ANALOG IN 1-8", "ANALOG IN 9-16", "ADAT IN 1-8", "ADAT IN 9-16", "ADAT IN 17-24", "ADAT IN 25-32", "MADI IN 1-8", "MADI IN 9-16"};
inline const char* kOutputBlockNames[6] = {"ANALOG OUT 1-8", "ANALOG OUT 9-16", "ADAT OUT 1-8", "ADAT OUT 9-16", "ADAT OUT 17-24", "ADAT OUT 25-32"};

// ---- State blocks the Pulse sends after a Load (and probably on request) ----
// 50 00 + 16 input gains (dB, 0..28) + 16 output levels (28 + dB). CONFIRMED against the user's setup.
// 51 00 + 22 bytes: one source column per output block row, same row/col numbering as route().
//   Seen: 02 03 00 01 04 05 | 06 07 08 09 0a 0b 0c 0d | 06 07 08 09 0a 0b 0c 0d.
//   Rows 0..5 are analog and ADAT outs, rows 6..13 MADI (col = row). Rows 14..21 are unidentified (probably Dante).
// 12 00 (140 bytes) and 13 00 (76 bytes) are not decoded.
struct Pulse16State {
    int inputGainDb[16]{};
    int outputLevelDb[16]{};
    uint8_t routeCol[22]{};
    // From the 47 byte block (01 00) sent on connect and after Load. CONFIRMED against captures:
    //   body[1] clock source, body[2] master sample rate index, body[3] headphone volume (vv, dB = vv - 79),
    //   body[4] headphone source, body[5] headphone channel code, body[34] active preset 1..6.
    int hpVolumeVv = 0, hpSource = 1, hpChannelCode = 0x40, activePreset = 0;
    bool settingsKnown = false;
    Clock clock = Clock::Master;   // valid only when clockKnown: set from echoes, the connect blocks holding it are not decoded yet
    Rate rate = Rate::k48;         // valid only when rateKnown
    bool clockKnown = false, rateKnown = false;
};
inline bool parseGains(const Bytes& m, Pulse16State& s) {
    if (m.size() != 44 || m[9] != 0x50 || m[10] != 0x00) return false;
    for (int i = 0; i < 16; ++i) { s.inputGainDb[i] = m[11 + i]; s.outputLevelDb[i] = int(m[27 + i]) - 28; }
    return true;
}
inline bool parseSettings(const Bytes& m, Pulse16State& s) {
    if (m.size() != 47 || m[9] != 0x01 || m[10] != 0x00) return false;
    const uint8_t* b = m.data() + 11;                 // body after 01 00
    if (b[1] > 2 || b[2] > 8) return false;
    s.clock = Clock(b[1]); s.clockKnown = true;
    s.rate = Rate(b[2]); s.rateKnown = true;
    s.hpVolumeVv = b[3]; s.hpSource = b[4]; s.hpChannelCode = b[5]; s.activePreset = b[34];
    s.settingsKnown = true;
    return true;
}
inline bool parseRouting(const Bytes& m, Pulse16State& s) {
    if (m.size() != 34 || m[9] != 0x51 || m[10] != 0x00) return false;
    for (int i = 0; i < 22; ++i) s.routeCol[i] = m[11 + i];
    return true;
}

} // namespace wm::pulse16
