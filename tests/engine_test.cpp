#include <cstdio>
#include <cstdlib>
#include "../src/engine/AudioEngine.h"
using namespace wm;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(float a, float b, float t = 0.02f) { return std::fabs(a - b) <= t; }

static void run(engine::AudioEngine& e, std::vector<std::vector<float>>& in, std::vector<std::vector<float>>& out, int blocks = 40) {
    std::vector<const float*> ip; for (auto& v : in) ip.push_back(v.data());
    std::vector<float*> op; for (auto& v : out) op.push_back(v.data());
    for (int i = 0; i < blocks; ++i) e.process(ip.data(), op.data(), int(in[0].size()));
}

int main() {
    mixer::Mixer m(2, 1); mixer::RoutingMatrix r(2, 1);
    engine::AudioEngine e(m, r);
    std::vector<std::vector<float>> in(2, std::vector<float>(256, 0.5f)), out(2, std::vector<float>(256));
    run(e, in, out);
    CHECK(near(out[0][255], 0.0f, 1e-6f));            // nothing routed: silence
    r.set(0, 0, 0.0f);
    run(e, in, out);
    CHECK(near(out[0][255], 0.5f * 0.7071f));         // centre pan, constant power
    CHECK(near(out[1][255], 0.5f * 0.7071f));
    m.state().channels[0].pan = -1.0f; run(e, in, out);
    CHECK(near(out[0][255], 0.5f) && near(out[1][255], 0.0f));
    m.state().channels[0].pan = 0; m.state().channels[0].gainDb = -6.0206f; run(e, in, out);
    CHECK(near(out[0][255], 0.5f * 0.5f * 0.7071f));
    m.state().channels[0].gainDb = 0; m.state().channels[0].phaseL = true; run(e, in, out);
    CHECK(out[0][255] < 0);                           // phase invert
    m.state().channels[0].phaseL = false; m.state().channels[0].mute = true; run(e, in, out);
    CHECK(near(out[0][255], 0.0f, 1e-4f));            // mute
    m.state().channels[0].mute = false; m.state().channels[0].solo = true; r.set(1, 0, 0.0f); run(e, in, out);
    CHECK(near(out[0][255], 0.5f * 0.7071f));         // solo silences channel 2
    m.state().channels[0].solo = false; run(e, in, out);
    CHECK(near(out[0][255], 2 * 0.5f * 0.7071f));     // two channels sum
    m.state().buses[0].dim = true; run(e, in, out);
    CHECK(near(out[0][255], 2 * 0.5f * 0.7071f * 0.1f)); // dim -20 dB
    CHECK(near(e.inputPeakDb(0), -6.0206f, 0.1f));
    // zipper check: a gain jump must ramp, not step
    m.state().buses[0].dim = false; m.state().channels[1].mute = true; run(e, in, out);
    m.state().channels[1].mute = false;
    std::vector<const float*> ip{in[0].data(), in[1].data()}; std::vector<float*> op{out[0].data(), out[1].data()};
    e.process(ip.data(), op.data(), 256);
    CHECK(out[0][0] < out[0][255]);
    if (!fails) std::puts("engine_test: all passed");
    return fails ? 1 : 0;
}
