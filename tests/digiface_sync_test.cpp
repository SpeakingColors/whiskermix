#include <cstdio>
#include <cstdlib>
#include "../src/backends/digiface/DigifaceSync.h"
#include "../src/mixer/Layout.h"
using namespace wm::digiface;
using namespace wm::mixer;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

static int words(const std::vector<uint8_t>& b) { return int(b.size() / 4); }
static uint16_t valueAt(const std::vector<uint8_t>& b, int i) { return uint16_t(b[size_t(4 * i)] | (b[size_t(4 * i + 1)] << 8)); }
static uint8_t byteAt(const std::vector<uint8_t>& b, int i) { return b[size_t(4 * i + 2)]; }
static uint8_t flagAt(const std::vector<uint8_t>& b, int i) { return b[size_t(4 * i + 3)]; }

int main() {
    // Layout: the user's TotalMix setup
    auto l = studioLayout();
    CHECK(countRow(l, Row::HardwareInput) == 11 && countRow(l, Row::SoftwarePlayback) == 9);
    CHECK(countRow(l, Row::HardwareOutput) == 8 && countRow(l, Row::ControlRoom) == 2);
    auto b = buildFromLayout(l);
    CHECK(b.mixer.state().channels.size() == 20 && b.mixer.state().buses.size() == 10);
    CHECK(b.mixer.state().channels[0].name == "SYNTH" && b.mixer.state().channels[0].stereoLinked && b.mixer.state().channels[0].hwFirst == 0);
    CHECK(b.mixer.state().channels[1].name == "B1" && b.mixer.state().channels[1].hwFirst == 2 && !b.mixer.state().channels[1].playback);
    CHECK(b.mixer.state().channels[11].name == "COMPUTER" && b.mixer.state().channels[11].gainDb == 0.0f && b.mixer.state().channels[11].playback);
    CHECK(b.mixer.state().channels[19].name == "PH" && b.mixer.state().channels[19].hwFirst == 32 && b.mixer.state().channels[19].stereoLinked);
    CHECK(b.mixer.state().buses[0].name == "Main" && b.mixer.state().buses[0].hwFirst == 0 && std::abs(b.mixer.state().buses[0].volumeDb + 42.1f) < 0.01f);
    CHECK(b.mixer.state().buses[1].name == "iLoud Micro" && b.mixer.state().buses[1].hwFirst == 2 && b.mixer.state().buses[2].name == "ADAT 5/6" && b.mixer.state().buses[2].hwFirst == 4);
    CHECK(b.mixer.state().buses[7].name == "INSERT" && !b.mixer.state().buses[7].stereo && b.mixer.state().buses[7].hwFirst == 14);
    CHECK(b.routing.routed(11, 0) && !b.routing.routed(0, 0));   // COMPUTER plays to Main, SYNTH starts at -inf

    // The exact vendor block from the "all faders" capture: SYNTH (inputs 1+2) at the start of a gesture:
    // 48=0400 49=0601 4A=0000 4B=0201 (bind words) and the gain on all four handles.
    {
        auto w = stripBlock(kInputSlot, false, 0, true, 0x0020, 0x0020);
        CHECK(w.size() == 8);
        CHECK(w[1].value == 0x0400 && w[1].channel == 0x48 && w[1].flag == 0x40);
        CHECK(w[3].value == 0x0601 && w[3].channel == 0x49);
        CHECK(w[5].value == 0x0000 && w[5].channel == 0x4A);
        CHECK(w[7].value == 0x0201 && w[7].channel == 0x4B);
        // B1 (mono input 3): 0402 0602 0002 0202
        auto m = stripBlock(kInputSlot, false, 2, false, 0x11, 0x11);
        CHECK(m[1].value == 0x0402 && m[3].value == 0x0602 && m[5].value == 0x0002 && m[7].value == 0x0202);
        // COMPUTER (playback 1+2) on handles 0x40..: 0500 0701 0100 0301
        auto p = stripBlock(kPlaybackSlot, true, 0, true, 0x17, 0x17);
        CHECK(p[1].value == 0x0500 && p[3].value == 0x0701 && p[5].value == 0x0100 && p[7].value == 0x0301);
        // PH playback (channels 33+34): 0520 0721 0120 0321
        auto ph = stripBlock(kPlaybackSlot, true, 32, true, 0x17, 0x17);
        CHECK(ph[1].value == 0x0520 && ph[3].value == 0x0721 && ph[5].value == 0x0120 && ph[7].value == 0x0321);
    }
    // Output volume: ADAT 5/6 = wIndex 0x0104/0x0105, PH = 0x0120/0x0121, INSERT (mono) = 0x010E only
    {
        auto o = outputVolume(4, true, 0.0f); CHECK(o.size() == 2 && o[0].index == 0x0104 && o[1].index == 0x0105 && o[0].value == 0x9000);
        auto ph = outputVolume(32, true, -10.0f); CHECK(ph[0].index == 0x0120 && ph[1].index == 0x0121);
        auto ins = outputVolume(14, false, 0.0f); CHECK(ins.size() == 1 && ins[0].index == 0x010E);
    }

    // Sync against the studio layout: first write sends every strip block and every output, idle sends nothing
    RecordingTransport t;
    Sync s(t);
    Mixer& m = b.mixer;
    s.setMainBuses(0, 1);
    int n = s.sync(m);
    CHECK(n > 0);
    CHECK(t.bulk.size() == 1 && words(t.bulk[0]) == 20 * 8);     // 20 strips, 8 words each
    CHECK(t.control.size() == 18);                               // 8 stereo outputs x 2 + 2 mono outputs
    CHECK(s.sync(m) == 0);

    // Fader on B1 (mono input 3): one block of 8 words on handles 0x48..0x4B, gain 0x8B53 (-3 dB)
    t.bulk.clear();
    m.state().channels[1].gainDb = -3.0f; s.sync(m);
    CHECK(t.bulk.size() == 1 && words(t.bulk[0]) == 8);
    CHECK(byteAt(t.bulk[0], 0) == 0x48 && valueAt(t.bulk[0], 0) == 0x8B53 && flagAt(t.bulk[0], 0) == 0);
    CHECK(byteAt(t.bulk[0], 1) == 0x48 && valueAt(t.bulk[0], 1) == 0x0402 && flagAt(t.bulk[0], 1) == 0x40);

    // Mute = gain 0 on both sides; unmute writes the fader back
    t.bulk.clear();
    m.state().channels[1].mute = true; s.sync(m);
    CHECK(t.bulk.size() == 1 && valueAt(t.bulk[0], 0) == 0 && valueAt(t.bulk[0], 2) == 0);
    m.state().channels[1].mute = false; t.bulk.clear(); s.sync(m);
    CHECK(valueAt(t.bulk[0], 0) == 0x8B53);

    // Solo on COMPUTER silences the other strips that were not already silent; COMPUTER keeps 0 dB
    t.bulk.clear();
    m.state().channels[11].solo = true; s.sync(m);
    CHECK(t.bulk.size() == 1 && words(t.bulk[0]) == 2 * 8);      // BROWSER (0 dB) and B1 (-3 dB) change; the rest were -inf
    CHECK(s.written().at(1000 + 0).l == 0x9000 && s.written().at(2).l == 0);
    m.state().channels[11].solo = false; s.sync(m);

    // Pan right on COMPUTER: left output paths (handles 0x40 and 0x42) go to 0, right paths keep 0 dB
    t.bulk.clear();
    m.state().channels[11].pan = 1.0f; s.sync(m);
    CHECK(words(t.bulk[0]) == 8 && byteAt(t.bulk[0], 0) == 0x40 && valueAt(t.bulk[0], 0) == 0 && valueAt(t.bulk[0], 2) == 0x9000 && valueAt(t.bulk[0], 4) == 0);
    m.state().channels[11].pan = 0.0f; s.sync(m);

    // Phase R on SYNTH: right paths get 0xF000
    t.bulk.clear();
    m.state().channels[0].phaseR = true; s.sync(m);
    CHECK(words(t.bulk[0]) == 8 && valueAt(t.bulk[0], 2) == kPhaseInvert && valueAt(t.bulk[0], 0) == 0);   // SYNTH fader is -inf
    m.state().channels[0].phaseR = false; s.sync(m);

    // M/S on SYNTH (already stereo): the 16-word burst, then the strip block again
    t.bulk.clear();
    m.state().channels[0].midSide = true; s.sync(m);
    CHECK(t.bulk.size() == 2 && words(t.bulk[0]) == 16 && valueAt(t.bulk[0], 0) == 0x8800 && valueAt(t.bulk[0], 1) == 0x4000);
    CHECK(byteAt(t.bulk[0], 8) == 0x48 && flagAt(t.bulk[0], 8) == 0x40 && valueAt(t.bulk[0], 12) == 0x0600);   // cross path L -> right out on 0x4C
    t.bulk.clear();
    m.state().channels[0].midSide = false; m.state().channels[0].stereoLinked = false; s.sync(m);
    CHECK(words(t.bulk[0]) == 8 && valueAt(t.bulk[0], 0) == 0x9000 && valueAt(t.bulk[0], 7) == 0);   // pair off burst
    m.state().channels[0].stereoLinked = true; s.sync(m);   // back to stereo: stereoOn burst
    t.bulk.clear(); s.sync(m); CHECK(t.bulk.empty());

    // Main volume and dim: outputs 0/1; speaker B switch: Main pair 0, iLoud pair (2/3) live
    t.control.clear();
    m.state().buses[0].volumeDb = -30.0f; s.sync(m);
    CHECK(t.control.size() == 2 && t.control[0].index == 0x0100 && t.control[1].index == 0x0101);
    t.control.clear();
    m.state().buses[0].dim = true; s.sync(m);
    CHECK(t.control.size() == 2 && std::abs(decodeGain(t.control[0].value) + 50.0f) < 0.3f);
    t.control.clear();
    s.setSpeakerB(true); s.sync(m);
    CHECK(t.control.size() == 4 && t.control[0].index == 0x0100 && t.control[0].value == 0 && t.control[2].index == 0x0102 && t.control[2].value != 0);
    t.control.clear(); s.sync(m); CHECK(t.control.empty());
    s.setSpeakerB(false); s.sync(m);

    // Hardware output PH at -10 dB was written on 0x0120/0x0121 at the first sync
    CHECK(s.outputs().count(0x0120) && std::abs(decodeGain(uint16_t(s.outputs().at(0x0120))) + 10.0f) < 0.05f);

    // Main mono bursts, then every strip again
    t.bulk.clear();
    s.setMainMono(true); s.sync(m);
    CHECK(words(t.bulk[0]) == 24 && valueAt(t.bulk[0], 0) == 0x8800 && byteAt(t.bulk[0], 0) == 0x40 && t.bulk.size() == 2);
    t.bulk.clear();
    s.setMainMono(false); s.sync(m);
    CHECK(words(t.bulk[0]) == 16 && t.bulk.size() == 2);

    // invalidate: everything is sent again
    t.bulk.clear(); t.control.clear();
    s.invalidate(); s.sync(m);
    CHECK(!t.bulk.empty() && !t.control.empty());

    std::puts("digiface sync + layout: passed");
    return 0;
}
