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
    Frames f; f.id = {1, 2, 3, 4, 5};   // dummy id: the real one comes from the unit
    // codec
    SAME(f.clockSource(int(Clock::Master)), "f00011220102030405010100f7");
    SAME(f.clockSource(int(Clock::Bnc)), "f00011220102030405010102f7");
    SAME(f.sampleRate(int(Rate::k32)), "f00011220102030405010200f7");
    SAME(f.sampleRate(int(Rate::k48)), "f00011220102030405010202f7");
    SAME(f.sampleRate(int(Rate::k192)), "f00011220102030405010208f7");
    SAME(f.displayBrightness(15), "f00011220102030405010e0ff7");
    SAME(f.keyClick(true), "f00011220102030405010d01f7");
    SAME(f.headphoneSource(int(HpSource::AdatIn)), "f00011220102030405010402f7");
    SAME(f.headphoneStereo(33), "f00011220102030405010550f7");
    SAME(f.headphoneStereo(1), "f00011220102030405010540f7");
    SAME(f.headphoneMono(33), "f00011220102030405010520f7");
    SAME(f.headphoneVolumeDb(0), "f0001122010203040501034ff7");
    SAME(f.outputLevel(1, 0), "f0001122010203040510101cf7");
    SAME(f.inputGain(1, 20), "f00011220102030405100014f7");
    SAME(Frames::probe(), "f000112200000000000200f7");
    SAME(f.hello(), "f000112201020304050201f7");
    SAME(f.savePreset(1), "f00011220102030405020700f7");
    SAME(f.savePreset(6), "f00011220102030405020705f7");
    SAME(f.loadPreset(1), "f0001122010203040502060007f7");
    SAME(f.loadPreset(6), "f0001122010203040502060507f7");

    // session: probe first, the id is learned from the answer, then 02 01 goes out with that id
    Rec r; Session s(r);
    s.connect(1000);
    assert(r.sent.size() == 1 && hex(r.sent[0]) == "f000112200000000000200f7");
    assert(s.status() == Status::Probing && !s.connected() && !s.haveState());
    s.setInputGain(1, 5); assert(r.sent.size() == 1);                       // nothing is sent before the unit answered
    s.onSysEx(unhex("f000112201020304050300f7"));
    assert(s.connected() && r.sent.size() == 2 && hex(r.sent[1]) == "f000112201020304050201f7");
    assert(s.unitId()[0] == 1 && s.unitId()[4] == 5);
    s.onSysEx(unhex("f00011220909090909010101f7"));                         // another unit's frame is ignored
    assert(!s.state().clockKnown);
    s.onSysEx(unhex("f0001122010203040550001414000014140e0e0e0e0000080800001c1c1c1c1c1c1c1c1c1c1c1c1c1c1c1cf7"));
    assert(!s.haveState());
    s.onSysEx(unhex("f000112201020304055100020300010405060708090a0b0c0d060708090a0b0c0df7"));
    assert(s.haveState());
    const auto& st = s.state();
    assert(st.inputGainDb[0] == 20 && st.inputGainDb[6] == 14 && st.inputGainDb[12] == 8 && st.inputGainDb[2] == 0);
    assert(st.outputLevelDb[0] == 0 && st.outputLevelDb[15] == 0);
    assert(st.routeCol[0] == 2 && st.routeCol[1] == 3 && st.routeCol[2] == 0 && st.routeCol[3] == 1 && st.routeCol[13] == 13 && st.routeCol[21] == 13);
    assert(!st.clockKnown && !st.rateKnown);
    // the 47 byte block from a real connect (clock BNC, 48k, headphone -40 dB, Analog Out, 33+34, preset 1)
    s.onSysEx(unhex("f00011220102030405010000020227015001000101000000010f0000000000000000000000000000004600200001f7"));
    assert(st.settingsKnown && st.clock == Clock::Bnc && st.rate == Rate::k48 && st.hpVolumeVv == 0x27 && st.hpVolumeVv - 79 == -40);
    assert(st.hpSource == 1 && st.hpChannelCode == 0x50 && st.activePreset == 1);
    s.onSysEx(unhex("f00011220102030405010000000227015001000101000000010f0000000000000000000000000000004600200003f7"));
    assert(st.clock == Clock::Master && st.activePreset == 3);
    // echoes update the model (rate, clock, headphone, front panel fader and routing moves)
    s.onSysEx(unhex("f000112201020304050102" "02f7")); assert(s.state().rateKnown && s.state().rate == Rate::k48);
    s.onSysEx(unhex("f000112201020304050101" "01f7")); assert(s.state().clockKnown && s.state().clock == Clock::Adat);
    s.onSysEx(unhex("f00011220102030405010327f7")); assert(s.state().hpVolumeVv == 0x27);
    s.onSysEx(unhex("f0001122010203040510" "0306f7")); assert(s.state().inputGainDb[3] == 6);
    s.onSysEx(unhex("f0001122010203040510" "1210f7")); assert(s.state().outputLevelDb[2] == -12);
    s.onSysEx(unhex("f0001122010203040511" "0709f7")); assert(s.state().routeCol[7] == 9);
    // meters: 32 raw bytes after 04 00, two constant bytes after them (taken from a capture, values 0 to 3 only)
    assert(!s.state().metersKnown);
    s.onSysEx(unhex("f00011220102030405040001010101010101010101010101010101010202010102010101010101010101010101f7"));
    assert(s.state().metersKnown && s.state().meters[0] == 1 && s.state().meters[17] == 2 && signalPresent(s.state().meters[17]) && !signalPresent(s.state().meters[0]));
    // setters send and update; the rate is sent even if the model already holds it
    r.sent.clear();
    s.setRate(Rate::k48); s.setRate(Rate::k48);
    assert(r.sent.size() == 2 && hex(r.sent[0]) == "f000112201020304050102" "02f7");
    s.setClock(Clock::Master); assert(s.state().clock == Clock::Master && hex(r.sent.back()) == "f00011220102030405010100f7");
    s.setInputGain(3, 99); assert(s.state().inputGainDb[2] == 28);
    s.setRoute(14, 0); s.setRoute(0, 14); assert(r.sent.size() == 4);      // out of range cells are refused
    // meter polling doubles as keep-alive: one poll per 220 ms, and silence for 3 s means Lost
    r.sent.clear();
    s.tick(1100); assert(r.sent.empty());                                  // 100 ms after connect: no poll yet
    s.tick(1300); assert(r.sent.size() == 1 && hex(r.sent.back()) == "f000112201020304050204f7");
    s.tick(1400); assert(r.sent.size() == 1);
    s.tick(4500); assert(s.status() == Status::Lost);                      // nothing heard for more than 3 s
    s.onSysEx(unhex("f00011220102030405010202f7")); assert(s.status() == Status::Connected);   // any frame brings it back
    // loading a preset invalidates the state until the unit replies
    s.loadPresetSlot(2); assert(!s.haveState() && hex(r.sent.back()) == "f00011220102030405020601" "07f7");
    // no answer to the probe, and no state blocks after the probe answer
    { Rec r2; Session s2(r2); s2.connect(0); s2.tick(1000); assert(s2.status() == Status::Probing); s2.tick(1600); assert(s2.status() == Status::NoAnswer); }
    { Rec r3; Session s3(r3); s3.connect(0); s3.onSysEx(unhex("f000112201020304050300f7")); s3.tick(3100); assert(s3.status() == Status::NoAnswer); }
    // backup and restore
    {
        Pulse16State a; for (int i = 0; i < 16; ++i) { a.inputGainDb[i] = i; a.outputLevelDb[i] = -i; } for (int i = 0; i < 22; ++i) a.routeCol[i] = uint8_t(i % 14);
        a.clock = Clock::Bnc; a.rate = Rate::k96; a.hpVolumeVv = 40; a.hpSource = 3; a.hpChannelCode = 0x41;
        std::string t = toText(a); Pulse16State b;
        assert(fromText(t, b) && b.inputGainDb[15] == 15 && b.outputLevelDb[15] == -15 && b.routeCol[13] == 13 && b.clock == Clock::Bnc && b.rate == Rate::k96 && b.hpVolumeVv == 40 && b.hpSource == 3 && b.hpChannelCode == 0x41);
        assert(!fromText("garbage", b) && !fromText("whiskermix-pulse16-backup 1\ngain=1,2\n", b));
        Rec r4; Session s4(r4); s4.connect(0); s4.onSysEx(unhex("f000112201020304050300f7"));
        r4.sent.clear(); int n = s4.applyState(b);
        assert(n == 32 + 14 + 2 + 2 + 1 && int(r4.sent.size()) == n);
        assert(hex(r4.sent[0]) == "f0001122010203040510" "0000f7" && hex(r4.sent.back()) == "f00011220102030405010541" "f7");
    }
    // UI helpers
    assert(inputGainToDbu(0) == 20 && inputGainToDbu(14) == 6 && inputGainToDbu(28) == -8 && dbuToInputGain(20) == 0 && dbuToInputGain(99) == 0 && dbuToInputGain(-99) == 28);
    assert(outputLevelToDbu(0) == 20 && dbuToOutputLevel(20) == 0 && dbuToOutputLevel(-30) == -28);
    { uint8_t raw[] = {0x00, 0x11, 0x22, 0x00}; auto f = withFraming(raw, 4); assert(f.size() == 6 && f.front() == 0xF0 && f.back() == 0xF7); }
    { int ch = 0; bool stereo = false;
      assert(decodeHeadphoneChannel(0x50, ch, stereo) && ch == 33 && stereo);
      assert(decodeHeadphoneChannel(0x40, ch, stereo) && ch == 1 && stereo);
      assert(decodeHeadphoneChannel(0x20, ch, stereo) && ch == 33 && !stereo);
      assert(decodeHeadphoneChannel(0x00, ch, stereo) && ch == 1 && !stereo);
      assert(!decodeHeadphoneChannel(0x60, ch, stereo)); }
    printf("pulse16 ok\n");
    return 0;
}
