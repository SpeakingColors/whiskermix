#include <cstdio>
#include <cstdlib>
#include "../src/mixer/MixerModel.h"
using namespace wm::mixer;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)
int main() {
    Mixer m(3, 1);
    m.rename(0, "Kick");
    CHECK(m.state().channels[0].name == "Kick");
    m.state().channels[1].mute = true;
    CHECK(m.effectiveGainDb(1) < -100.0f && m.effectiveGainDb(0) == 0.0f);
    m.state().channels[2].solo = true;
    CHECK(m.effectiveGainDb(0) < -100.0f && m.effectiveGainDb(2) == 0.0f);
    m.state().channels[2].solo = false; m.state().channels[1].mute = false;
    m.state().channels[0].gainDb = -6.0f;
    m.store("Mix");
    m.state().channels[0].gainDb = 0.0f;
    CHECK(m.recall("Mix") && m.state().channels[0].gainDb == -6.0f && !m.recall("none"));
    m.state().channels[0].pan = 1.0f;
    auto w = m.digifaceWords(0);
    CHECK(w[0].value == 0 && w[1].value != 0);          // pan right silences the left side
    m.state().channels[0].phaseR = true;
    CHECK(m.digifaceWords(0)[1].value == wm::digiface::kPhaseInvert);
    m.state().buses[0].volumeDb = -30.0f; m.state().buses[0].dim = true;
    auto mw = m.mainWrites(0, 0);
    CHECK(mw.size() == 2 && mw[0].index == 0x0100 && mw[1].index == 0x0101);
    CHECK(std::abs(wm::digiface::decodeGain(mw[0].value) - (-50.0f)) < 0.3f);
    // Stereo split and join (the TotalMix "Stereo" button)
    Mixer st(2, 1);
    st.state().channels[0].name = "SYNTH"; st.state().channels[0].stereoLinked = true; st.state().channels[0].hwFirst = 0; st.state().channels[0].gainDb = -4.0f;
    st.state().channels[1].name = "B1"; st.state().channels[1].hwFirst = 2;
    CHECK(st.splitStereo(0) && st.state().channels.size() == 3);
    CHECK(st.state().channels[0].name == "SYNTH 1" && st.state().channels[1].name == "SYNTH 2" && st.state().channels[2].name == "B1");
    CHECK(st.state().channels[0].hwFirst == 0 && st.state().channels[1].hwFirst == 1 && !st.state().channels[1].stereoLinked && st.state().channels[1].gainDb == -4.0f);
    CHECK(!st.splitStereo(0));                      // already mono
    CHECK(!st.canJoin(1));                          // SYNTH 2 (hw 1) and B1 (hw 2) are not a hardware pair (pairs start on 1, 3, 5...)
    CHECK(st.canJoin(0) && st.joinStereo(0) && st.state().channels.size() == 2);
    CHECK(st.state().channels[0].name == "SYNTH" && st.state().channels[0].stereoLinked && st.state().channels[1].name == "B1");
    std::puts("mixer: passed");
    return 0;
}
