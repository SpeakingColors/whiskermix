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

int main() {
    // Layout: the user's TotalMix setup
    auto l = studioLayout();
    CHECK(countRow(l, Row::HardwareInput) == 11 && countRow(l, Row::SoftwarePlayback) == 9);
    CHECK(countRow(l, Row::HardwareOutput) == 8 && countRow(l, Row::ControlRoom) == 2);
    auto b = buildFromLayout(l);
    CHECK(b.mixer.state().channels.size() == 20 && b.mixer.state().buses.size() == 10);
    CHECK(b.mixer.state().channels[0].name == "SYNTH" && b.mixer.state().channels[0].stereoLinked);
    CHECK(b.mixer.state().channels[11].name == "COMPUTER" && b.mixer.state().channels[11].gainDb == 0.0f);
    CHECK(b.mixer.state().buses[0].name == "Main" && std::abs(b.mixer.state().buses[0].volumeDb + 42.1f) < 0.01f);
    CHECK(b.mixer.state().buses[1].name == "iLoud Micro" && b.mixer.state().buses[2].name == "ADAT 5/6");
    CHECK(b.routing.routed(11, 0) && !b.routing.routed(0, 0));   // COMPUTER plays to Main, SYNTH starts at -inf

    // Sync: first write sends everything, idle sends nothing
    RecordingTransport t;
    Sync s(t);
    Mixer m(4, 2);
    s.setInputRange(0, 4);
    s.setMainBuses(0, 1);
    int n = s.sync(m);
    CHECK(n > 0);
    CHECK(t.bulk.size() == 1 && words(t.bulk[0]) == 8);   // all gains in one transfer, no bursts at start
    CHECK(s.sync(m) == 0);                           // nothing changed

    // Fader: two words, right bytes
    t.bulk.clear();
    m.state().channels[1].gainDb = -3.0f;
    s.sync(m);
    CHECK(t.bulk.size() == 1 && words(t.bulk[0]) == 2);
    CHECK(byteAt(t.bulk[0], 0) == 0x4A && byteAt(t.bulk[0], 1) == 0x4B && valueAt(t.bulk[0], 0) == 0x8B53);

    // Mute = gain 0 on both sides; unmute writes the fader back
    t.bulk.clear();
    m.state().channels[1].mute = true; s.sync(m);
    CHECK(t.bulk.size() == 1 && valueAt(t.bulk[0], 0) == 0 && valueAt(t.bulk[0], 1) == 0);
    m.state().channels[1].mute = false; t.bulk.clear(); s.sync(m);
    CHECK(valueAt(t.bulk[0], 0) == 0x8B53);

    // Solo on channel 2 silences 0, 1 and 3 but not 2
    t.bulk.clear();
    m.state().channels[2].solo = true; s.sync(m);
    CHECK(t.bulk.size() == 1 && words(t.bulk[0]) == 6);
    CHECK(s.written().at(0x4C) == 0x9000 && s.written().at(0x48) == 0 && s.written().at(0x4E) == 0);
    m.state().channels[2].solo = false; s.sync(m);

    // Pan right: left side down, right side unchanged
    t.bulk.clear();
    m.state().channels[0].pan = 1.0f; s.sync(m);
    CHECK(words(t.bulk[0]) == 1 && byteAt(t.bulk[0], 0) == 0x48 && valueAt(t.bulk[0], 0) == 0);
    m.state().channels[0].pan = 0.0f; s.sync(m);

    // Phase R on channel 0
    t.bulk.clear();
    m.state().channels[0].phaseR = true; s.sync(m);
    CHECK(words(t.bulk[0]) == 1 && byteAt(t.bulk[0], 0) == 0x49 && valueAt(t.bulk[0], 0) == kPhaseInvert);
    m.state().channels[0].phaseR = false; s.sync(m);

    // Stereo link on pair 0: the captured 12-word burst, then no gain rewrite for bytes the burst set
    t.bulk.clear();
    m.state().channels[0].stereoLinked = true; s.sync(m);
    CHECK(t.bulk.size() >= 1 && words(t.bulk[0]) == 12 && valueAt(t.bulk[0], 0) == 0x8B50 && byteAt(t.bulk[0], 8) == 0x4C);
    CHECK(t.bulk[0][4 * 8 + 3] == 0x40);   // flag word
    // M/S replaces the stereo burst
    t.bulk.clear();
    m.state().channels[0].midSide = true; s.sync(m);
    CHECK(words(t.bulk[0]) == 12 && valueAt(t.bulk[0], 1) == 0x4000);
    // both off: 8-word burst
    t.bulk.clear();
    m.state().channels[0].midSide = false; m.state().channels[0].stereoLinked = false; s.sync(m);
    CHECK(words(t.bulk[0]) == 8 && valueAt(t.bulk[0], 0) == 0x9000 && valueAt(t.bulk[0], 7) == 0);
    // other pairs: no burst (not captured yet), link is software only
    t.bulk.clear();
    m.state().channels[2].stereoLinked = true; s.sync(m);
    CHECK(t.bulk.empty());

    // Main A volume and dim, speaker B switch
    t.control.clear();
    m.state().buses[0].volumeDb = -30.0f; s.sync(m);
    CHECK(t.control.size() == 2 && t.control[0].index == 0x0100 && t.control[1].index == 0x0101);
    t.control.clear();
    m.state().buses[0].dim = true; s.sync(m);
    CHECK(t.control.size() == 2 && std::abs(decodeGain(t.control[0].value) + 50.0f) < 0.3f);
    t.control.clear();
    s.setSpeakerB(true); m.state().buses[1].volumeDb = -23.0f; s.sync(m);
    CHECK(t.control.size() == 4 && t.control[0].value == 0 && t.control[2].index == 0x0102 && t.control[2].value != 0);
    t.control.clear(); s.sync(m); CHECK(t.control.empty());

    // Main mono bursts
    t.bulk.clear();
    s.setMainMono(true); s.sync(m);
    CHECK(words(t.bulk[0]) == 24 && valueAt(t.bulk[0], 0) == 0x8800 && byteAt(t.bulk[0], 0) == 0x40);
    CHECK(t.bulk.size() == 2 && words(t.bulk[1]) == 8);   // the pair gains are sent again after the burst
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
