// Frames and blocks below were captured from RemoteFish 2.1 talking to a Pulse 16 DX (firmware v5.04).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../src/backends/pulse16/Pulse16Session.h"
using namespace wm::pulse16;

static std::string hex(const Bytes& b) { char t[4]; std::string s; for (auto x : b) { snprintf(t, 4, "%02x", x); s += t; } return s; }
static Bytes unhex(const char* s) { Bytes b; for (; s[0] && s[1]; s += 2) { char t[3] = {s[0], s[1], 0}; b.push_back(uint8_t(strtol(t, nullptr, 16))); } return b; }
struct Rec : Transport { std::vector<Bytes> sent; void sendSysEx(const Bytes& m) override { sent.push_back(m); } };
#define SAME(b, h) do { if (hex(b) != (h)) { printf("FAIL line %d: %s != %s\n", __LINE__, hex(b).c_str(), h); return 1; } } while (0)

int main() {
    // codec
    SAME(clockSource(Clock::Master), "f00011220b3c454f0b010100f7");
    SAME(clockSource(Clock::Bnc), "f00011220b3c454f0b010102f7");
    SAME(sampleRate(Rate::k32), "f00011220b3c454f0b010200f7");
    SAME(sampleRate(Rate::k48), "f00011220b3c454f0b010202f7");
    SAME(sampleRate(Rate::k192), "f00011220b3c454f0b010208f7");
    SAME(displayBrightness(15), "f00011220b3c454f0b010e0ff7");
    SAME(keyClick(true), "f00011220b3c454f0b010d01f7");
    SAME(headphoneSource(HpSource::AdatIn), "f00011220b3c454f0b010402f7");
    SAME(headphoneStereo(33), "f00011220b3c454f0b010550f7");
    SAME(headphoneStereo(1), "f00011220b3c454f0b010540f7");
    SAME(headphoneMono(33), "f00011220b3c454f0b010520f7");
    SAME(headphoneVolumeDb(0), "f00011220b3c454f0b01034ff7");
    SAME(outputLevel(1, 0), "f00011220b3c454f0b10101cf7");
    SAME(inputGain(1, 20), "f00011220b3c454f0b100014f7");
    SAME(probe(), "f000112200000000000200f7");
    SAME(hello(), "f00011220b3c454f0b0201f7");
    SAME(savePreset(1), "f00011220b3c454f0b020700f7");
    SAME(savePreset(6), "f00011220b3c454f0b020705f7");
    SAME(loadPreset(1), "f00011220b3c454f0b02060007f7");
    SAME(loadPreset(6), "f00011220b3c454f0b02060507f7");

    // session: connect handshake and the state blocks captured right after it
    Rec r; Session s(r);
    s.connect();
    assert(r.sent.size() == 2 && hex(r.sent[0]) == "f000112200000000000200f7" && hex(r.sent[1]) == "f00011220b3c454f0b0201f7");
    assert(!s.connected() && !s.haveState());
    s.onSysEx(unhex("f00011220b3c454f0b0300f7"));
    assert(s.connected());
    s.onSysEx(unhex("f00011220b3c454f0b50001414000014140e0e0e0e0000080800001c1c1c1c1c1c1c1c1c1c1c1c1c1c1c1cf7"));
    assert(!s.haveState());
    s.onSysEx(unhex("f00011220b3c454f0b5100020300010405060708090a0b0c0d060708090a0b0c0df7"));
    assert(s.haveState());
    const auto& st = s.state();
    assert(st.inputGainDb[0] == 20 && st.inputGainDb[6] == 14 && st.inputGainDb[12] == 8 && st.inputGainDb[2] == 0);
    assert(st.outputLevelDb[0] == 0 && st.outputLevelDb[15] == 0);
    assert(st.routeCol[0] == 2 && st.routeCol[1] == 3 && st.routeCol[2] == 0 && st.routeCol[3] == 1 && st.routeCol[13] == 13 && st.routeCol[21] == 13);
    assert(!st.clockKnown && !st.rateKnown);
    // echoes update the model
    s.onSysEx(unhex("f00011220b3c454f0b010202f7")); assert(s.state().rateKnown && s.state().rate == Rate::k48);
    s.onSysEx(unhex("f00011220b3c454f0b010101f7")); assert(s.state().clockKnown && s.state().clock == Clock::Adat);
    // setters send and update; the rate is sent even if the model already holds it
    r.sent.clear();
    s.setRate(Rate::k48); s.setRate(Rate::k48);
    assert(r.sent.size() == 2 && hex(r.sent[0]) == "f00011220b3c454f0b010202f7");
    s.setClock(Clock::Master); assert(s.state().clock == Clock::Master && hex(r.sent.back()) == "f00011220b3c454f0b010100f7");
    s.setInputGain(3, 99); assert(s.state().inputGainDb[2] == 28);
    // loading a preset invalidates the state until the unit replies
    s.loadPresetSlot(2); assert(!s.haveState() && hex(r.sent.back()) == "f00011220b3c454f0b02060107f7");
    printf("pulse16 ok\n");
    return 0;
}
