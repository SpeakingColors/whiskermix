// Ferrofish Pulse 16 DX SysEx codec: frame builders and state-block parsers. Pure C++, no hardware calls.
// Tested in tests/pulse16_test.cpp. Protocol notes: docs/PULSE16-PROTOCOL.md.
// Source: USBPcap captures of RemoteFish 2.1 (firmware v5.04, ADAT-only DX), Oct 2026.
// Every value here was seen on the wire; items marked PROBABLE or UNVERIFIED were not proven.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>
#include <vector>

namespace wm::pulse16 {

using Bytes = std::vector<uint8_t>;

// Bytes after F0 00 11 22: five id bytes. The probe uses zeros; the unit's probe answer carries its real id, which
// Session learns and uses for every later frame (do not hardcode it, it may differ per unit).
using Id = std::array<uint8_t, 5>;
inline constexpr Id kBroadcastId{0, 0, 0, 0, 0};
// "0B 3C 45 4F 0B" style text for the UI. This is the unit id from the probe answer, NOT the serial number printed on the unit:
// RemoteFish shows a serial (SN nnn-nnn-nnn) but no captured frame carries it in any encoding tried (7-bit, nibbles, ASCII, BCD,
// plain arithmetic on the id). Where RemoteFish gets it is UNKNOWN: a capture of RemoteFish starting up is needed.
inline std::string idText(const Id& id) {
    static const char* h = "0123456789ABCDEF"; std::string t;
    for (size_t i = 0; i < id.size(); ++i) { if (i) t += ' '; t += h[id[i] >> 4]; t += h[id[i] & 15]; }
    return t;
}

// Frame builders for one unit id. All values were seen on the wire unless marked PROBABLE or UNVERIFIED.
struct Frames {
    Id id = kBroadcastId;

    Bytes frame(std::initializer_list<uint8_t> body) const {
        Bytes f{0xF0, 0x00, 0x11, 0x22, id[0], id[1], id[2], id[3], id[4]};
        f.insert(f.end(), body.begin(), body.end());
        f.push_back(0xF7);
        return f;
    }

    // Connect sequence seen from RemoteFish: probe (reply starts 03 00 and carries the id), then 02 01.
    // RemoteFish also sends 01 04 01 which selects headphone source Analog Out: WhiskerMix does not.
    static Bytes probe() { return {0xF0,0x00,0x11,0x22,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0xF7}; }
    Bytes hello() const { return frame({0x02, 0x01}); }          // the unit answers with its state blocks
    Bytes meterPoll() const { return frame({0x02, 0x04}); }      // RemoteFish polls about every 0.22 s; reply: see parseMeters

    // Faders. ch is 1..16.
    Bytes inputGain(int ch, int dB) const { return frame({0x10, uint8_t(ch - 1), uint8_t(std::clamp(dB, 0, 28))}); }      // 0..28 dB. CONFIRMED
    Bytes outputLevel(int ch, int dB) const { return frame({0x10, uint8_t(0x0F + ch), uint8_t(std::clamp(28 + dB, 0, 28))}); } // sent as 28 + dB (dB <= 0). CONFIRMED, lowest value not verified

    // Routing matrix cell. row = output block, col = input block (0-based, screen order).
    // col 0 ANALOG IN 1-8, 1 ANALOG IN 9-16, 2 ADAT IN 1-8, 3 ADAT 9-16, 4 ADAT 17-24, 5 ADAT 25-32, 6..13 MADI IN 1-8 .. 57-64.
    Bytes route(int row, int col) const { return frame({0x11, uint8_t(row), uint8_t(col)}); }

    // Headphone. CONFIRMED unless noted.
    Bytes headphoneVolume(int vv) const { return frame({0x01, 0x03, uint8_t(std::clamp(vv, 0, 79))}); } // 0 = Off, dB = vv - 79
    Bytes headphoneVolumeDb(int dB) const { return headphoneVolume(79 + dB); }                          // dB -78..0
    Bytes headphoneMute(bool on) const { return frame({0x05, 0x00, 0x06, uint8_t(on ? 1 : 0)}); }       // PROBABLE
    Bytes headphoneSource(int src) const { return frame({0x01, 0x04, uint8_t(src)}); }                  // HpSource
    Bytes headphoneStereo(int firstCh) const { return frame({0x01, 0x05, uint8_t(0x40 + (firstCh - 1) / 2)}); } // pair starting at odd channel
    Bytes headphoneMono(int ch) const { return frame({0x01, 0x05, uint8_t(ch - 1)}); }

    // Clock sync source. CONFIRMED. Master = the Pulse's own clock. Follow ADAT only if the other device is master.
    Bytes clockSource(int c) const { return frame({0x01, 0x01, uint8_t(c)}); }                           // Clock

    // Master sync frequency, CONFIRMED by dropdown order: 00 32k, 01 44.1k, 02 48k, 03 64k, 04 88.2k, 05 96k, 06 128k, 07 176.4k, 08 192k.
    // The GUI shows 48 kHz even when the unit runs another rate, and re-picking the shown value sends nothing,
    // so always send this explicitly. WARNING: on the ADAT-only DX, RemoteFish sent this same 01 02 command for its MIDI routing,
    // MADI 96k frame, MADI short frame and BNC termination controls. Do NOT map those to 01 02; their real commands are unknown.
    Bytes sampleRate(int r) const { return frame({0x01, 0x02, uint8_t(r)}); }                            // Rate

    Bytes displayBrightness(int v) const { return frame({0x01, 0x0E, uint8_t(std::clamp(v, 0, 15))}); } // 0 darkest, 15 brightest. CONFIRMED
    Bytes keyClick(bool on) const { return frame({0x01, 0x0D, uint8_t(on ? 1 : 0)}); }                  // PROBABLE
    // Language 00 = English PROBABLE (do not expose others). Delay compensation 01 10 00/01 PROBABLE.
    // Not implemented (command unknown): MIDI routing, MADI 96k frame, MADI short frame, BNC termination.

    // Presets, slots 1..6. CONFIRMED. Save N = 02 07 (N-1), ack 34 (overwrites the slot).
    // Load N = 02 06 (N-1) 07, ack 35, then the unit sends its state blocks. Headphone volume is not part of a preset.
    Bytes savePreset(int n) const { return frame({0x02, 0x07, uint8_t(std::clamp(n, 1, 6) - 1)}); }
    Bytes loadPreset(int n) const { return frame({0x02, 0x06, uint8_t(std::clamp(n, 1, 6) - 1), 0x07}); }
};

enum class HpSource : uint8_t { AnalogIn, AnalogOut, AdatIn, AdatOut, MadiIn, MadiOut, DanteIn, DanteOut };
enum class Clock : uint8_t { Master = 0, Adat = 1, Bnc = 2 };
enum class Rate : uint8_t { k32, k44_1, k48, k64, k88_2, k96, k128, k176_4, k192 };

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
inline const char* kInputBlockNames[14] = {"ANALOG IN 1-8", "ANALOG IN 9-16", "ADAT IN 1-8", "ADAT IN 9-16", "ADAT IN 17-24", "ADAT IN 25-32",
    "MADI IN 1-8", "MADI IN 9-16", "MADI IN 17-24", "MADI IN 25-32", "MADI IN 33-40", "MADI IN 41-48", "MADI IN 49-56", "MADI IN 57-64"};
inline const char* kOutputBlockNames[14] = {"ANALOG OUT 1-8", "ANALOG OUT 9-16", "ADAT OUT 1-8", "ADAT OUT 9-16", "ADAT OUT 17-24", "ADAT OUT 25-32",
    "MADI OUT 1-8", "MADI OUT 9-16", "MADI OUT 17-24", "MADI OUT 25-32", "MADI OUT 33-40", "MADI OUT 41-48", "MADI OUT 49-56", "MADI OUT 57-64"};

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
    // Meter poll reply (02 04): 32 raw bytes. EXPERIMENTAL: only near-silent captures exist (values 0 to 3), so the scale is
    // unvalidated; 0 or 1 = no signal, 2 or more = signal. Assumed order: 16 inputs then 16 outputs (UNVERIFIED).
    uint8_t meters[32]{};
    bool metersKnown = false;
};
inline bool signalPresent(uint8_t raw) { return raw >= 2; }
inline bool parseMeters(const Bytes& m, Pulse16State& s) {
    // F0 00 11 22 <id5> 04 00 <32 meter bytes> 01 01 F7 (46 bytes). The two trailing 01 are constant in every capture.
    if (m.size() != 46 || m[9] != 0x04 || m[10] != 0x00) return false;
    for (int i = 0; i < 32; ++i) s.meters[i] = m[11 + i];
    s.metersKnown = true;
    return true;
}
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


// ---- Backup file (plain text, one key per line) so a setup can be saved and restored ----
inline std::string toText(const Pulse16State& s) {
    std::ostringstream o;
    o << "whiskermix-pulse16-backup 1\n";
    auto list = [&](const char* k, const int* v, int n) { o << k << "="; for (int i = 0; i < n; ++i) o << (i ? "," : "") << v[i]; o << "\n"; };
    list("gain", s.inputGainDb, 16); list("level", s.outputLevelDb, 16);
    o << "route="; for (int i = 0; i < 22; ++i) o << (i ? "," : "") << int(s.routeCol[i]); o << "\n";
    o << "clock=" << int(s.clock) << "\nrate=" << int(s.rate) << "\nhpvol=" << s.hpVolumeVv << "\nhpsrc=" << s.hpSource << "\nhpch=" << s.hpChannelCode << "\n";
    return o.str();
}
inline bool fromText(const std::string& t, Pulse16State& out) {
    std::istringstream in(t); std::string line;
    if (!std::getline(in, line) || line.rfind("whiskermix-pulse16-backup 1", 0) != 0) return false;
    Pulse16State s; int got = 0;
    auto nums = [](const std::string& v) { std::vector<int> r; std::stringstream ss(v); std::string x; while (std::getline(ss, x, ',')) r.push_back(std::atoi(x.c_str())); return r; };
    while (std::getline(in, line)) {
        auto eq = line.find('='); if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq); auto v = nums(line.substr(eq + 1));
        if (k == "gain" && v.size() == 16) { for (int i = 0; i < 16; ++i) s.inputGainDb[i] = std::clamp(v[size_t(i)], 0, 28); ++got; }
        else if (k == "level" && v.size() == 16) { for (int i = 0; i < 16; ++i) s.outputLevelDb[i] = std::clamp(v[size_t(i)], -28, 0); ++got; }
        else if (k == "route" && v.size() == 22) { for (int i = 0; i < 22; ++i) s.routeCol[i] = uint8_t(std::clamp(v[size_t(i)], 0, 13)); ++got; }
        else if (k == "clock" && v.size() == 1 && v[0] >= 0 && v[0] <= 2) { s.clock = Clock(v[0]); s.clockKnown = true; ++got; }
        else if (k == "rate" && v.size() == 1 && v[0] >= 0 && v[0] <= 8) { s.rate = Rate(v[0]); s.rateKnown = true; ++got; }
        else if (k == "hpvol" && v.size() == 1) { s.hpVolumeVv = std::clamp(v[0], 0, 79); ++got; }
        else if (k == "hpsrc" && v.size() == 1) { s.hpSource = std::clamp(v[0], 0, 7); ++got; }
        else if (k == "hpch" && v.size() == 1) { s.hpChannelCode = v[0]; ++got; }
    }
    if (got != 8) return false;       // every key must be present and valid
    s.settingsKnown = true; out = s; return true;
}

} // namespace wm::pulse16
