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
    engine::AudioEngine e(m, r); e.prepare(256);
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
    // EQ: +12 dB mid at 1 kHz boosts a 1 kHz sine by ~4x; low cut removes DC
    {
        mixer::Mixer m2(1, 1); mixer::RoutingMatrix r2(1, 1); r2.set(0, 0, 0.0f);
        m2.state().channels[0].pan = -1.0f;
        auto& fx = m2.state().channels[0].fx; fx.eqOn = true; fx.midGain = 12; fx.midFreq = 1000; fx.midQ = 1;
        engine::AudioEngine e2(m2, r2); e2.prepare(4800);
        std::vector<float> sine(4800), o0(4800), o1(4800);
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.1f * std::sin(2 * 3.14159265f * 1000.0f * float(i) / 48000.0f);
        const float* ip2[1] = {sine.data()}; float* op2[2] = {o0.data(), o1.data()};
        for (int k = 0; k < 4; ++k) e2.process(ip2, op2, 4800);
        float pk = 0; for (size_t i = 2400; i < 4800; ++i) pk = std::max(pk, std::fabs(o0[i]));
        CHECK(near(pk, 0.1f * 3.98f, 0.03f));
        fx.eqOn = false; fx.compOn = true; fx.threshDb = -20; fx.ratio = 4; fx.kneeDb = 0; fx.attackMs = 1; fx.releaseMs = 50;
        for (int k = 0; k < 6; ++k) e2.process(ip2, op2, 4800);   // 0.1 peak = -20 dB: no reduction
        CHECK(near(e2.gainReductionDb(0), 0.0f, 0.5f));
        for (auto& v : sine) v *= 5.0f;                           // -6 dB: 14 over, out = -20+3.5 -> GR ~ -10.5
        for (int k = 0; k < 6; ++k) e2.process(ip2, op2, 4800);
        CHECK(near(e2.gainReductionDb(0), -10.5f, 1.0f));
        engine::Biquad bq = engine::Biquad::peak(48000, 1000, 12, 1);
        CHECK(near(bq.magDb(48000, 1000), 12.0f, 0.1f));
        CHECK(near(engine::compOutDb(fx, -8), -20 + 12 / 4.0f, 0.01f));
    }
    if (!fails) std::puts("engine_test: all passed");
    return fails ? 1 : 0;
}
