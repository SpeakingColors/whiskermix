#include <cassert>
#include <cmath>
#include <cstdio>
#include "../src/backends/digiface/DigifaceTransport.h"
using namespace wm::digiface;

static void near(float a, float b, float tol, const char* what) {
    if (std::abs(a - b) > tol) { std::printf("FAIL %s: %f vs %f\n", what, a, b); std::exit(1); }
}

int main() {
    // Reference points: value written by the vendor mixer for a given fader position.
    near(decodeGain(0x9000), 0.0f, 0.01f, "unity");
    near(decodeGain(0xA000), 6.02f, 0.02f, "top");
    near(decodeGain(0x8B53), -3.0f, 0.02f, "-3 dB");
    near(decodeGain(0x27FB), -10.1f, 0.05f, "-10.1 dB");
    near(decodeGain(0x8800), -6.02f, 0.02f, "mono sum");
    assert(encodeGain(0.0f) == 0x9000);
    assert(encodeGain(6.0f) <= 0xA000);
    assert(encodeGain(-200.0f) == 0);
    near(decodeGain(encodeGain(-3.0f)), -3.0f, 0.02f, "roundtrip -3");
    near(decodeGain(encodeGain(-10.1f)), -10.1f, 0.02f, "roundtrip -10.1");
    near(decodeGain(encodeGain(-40.0f)), -40.0f, 0.02f, "roundtrip -40");
    // Dim is the current level minus 20 dB (0x03B0 -> about 0x005E).
    near(decodeGain(0x005E), decodeGain(0x03B0) + kDimDb, 0.1f, "dim");

    RecordingTransport t;
    t.bulkOut(pack(channelGain(1, 0.0f)));
    assert(t.bulk[0].size() == 8 && t.bulk[0][2] == 0x4A && t.bulk[0][6] == 0x4B);
    auto pr = channelGainPan(0, 0.0f, 1.0f);       // full right: left side silent
    assert(pr[0].value == 0 && pr[1].value == 0x9000);
    auto sp = speakerSelect(1, -30.0f, -23.0f);
    assert(sp[0].value == 0 && sp[1].index == 0x0101 && sp[2].index == 0x0102 && sp[2].value != 0);
    std::puts("digiface codec: all tests passed");
    return 0;
}
