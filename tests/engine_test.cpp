#include <cstdio>
#include <thread>
#include <atomic>
#include <cstdlib>
#include "../src/engine/AudioEngine.h"
#include "../src/mixer/Layout.h"
using namespace wm;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
static bool near(float a, float b, float t = 0.02f) { return std::fabs(a - b) <= t; }

static void run(engine::AudioEngine& e, std::vector<std::vector<float>>& in, std::vector<std::vector<float>>& out, int blocks = 40) {
    std::vector<const float*> ip; for (auto& v : in) ip.push_back(v.data());
    std::vector<float*> op; for (auto& v : out) op.push_back(v.data());
    e.sync();
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
    e.sync(); e.process(ip.data(), op.data(), 256);
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
        e2.sync(); for (int k = 0; k < 4; ++k) e2.process(ip2, op2, 4800);
        float pk = 0; for (size_t i = 2400; i < 4800; ++i) pk = std::max(pk, std::fabs(o0[i]));
        CHECK(near(pk, 0.1f * 3.98f, 0.03f));
        fx.eqOn = false; fx.compOn = true; fx.threshDb = -20; fx.ratio = 4; fx.kneeDb = 0; fx.attackMs = 1; fx.releaseMs = 50;
        e2.sync(); for (int k = 0; k < 6; ++k) e2.process(ip2, op2, 4800);   // 0.1 peak = -20 dB: no reduction
        CHECK(near(e2.gainReductionDb(0), 0.0f, 0.5f));
        for (auto& v : sine) v *= 5.0f; e2.sync();                           // -6 dB: 14 over, out = -20+3.5 -> GR ~ -10.5
        for (int k = 0; k < 6; ++k) e2.process(ip2, op2, 4800);
        CHECK(near(e2.gainReductionDb(0), -10.5f, 1.0f));
        engine::Biquad bq = engine::Biquad::peak(48000, 1000, 12, 1);
        CHECK(near(bq.magDb(48000, 1000), 12.0f, 0.1f));
        CHECK(near(engine::compOutDb(fx, -8), -20 + 12 / 4.0f, 0.01f));
    }
    // Off = bypass: with both sections off the output equals the same channel with no FX at all; switching on changes it, off restores it
    {
        auto render = [](bool eq, bool comp) {
            mixer::Mixer mm(1, 1); mixer::RoutingMatrix rr(1, 1); rr.set(0, 0, 0.0f); mm.state().channels[0].pan = -1.0f;
            auto& f = mm.state().channels[0].fx; f.eqOn = eq; f.compOn = comp; f.midGain = 12; f.lowCutHz = 400; f.threshDb = -30; f.ratio = 8;
            engine::AudioEngine ee(mm, rr); ee.prepare(2400);
            std::vector<float> sn(2400), o0(2400), o1(2400);
            for (size_t i = 0; i < sn.size(); ++i) sn[i] = 0.3f * std::sin(2 * 3.14159265f * 1000.0f * float(i) / 48000.0f);
            const float* ip[1] = {sn.data()}; float* op[2] = {o0.data(), o1.data()};
            ee.sync(); for (int k = 0; k < 3; ++k) ee.process(ip, op, 2400);
            return std::make_pair(o0[2399], ee.gainReductionDb(0));
        };
        auto none = render(false, false), eqOnly = render(true, false), compOnly = render(false, true);
        CHECK(near(none.first, 0.3f * std::sin(2 * 3.14159265f * 1000.0f * 2399.0f / 48000.0f), 1e-5f));   // untouched
        CHECK(none.second == 0.0f);
        CHECK(!near(eqOnly.first, none.first, 0.01f));
        CHECK(compOnly.second < -3.0f);
        // same engine: on, then off again: the signal is exactly the plain one and the reduction reads 0
        mixer::Mixer mm(1, 1); mixer::RoutingMatrix rr(1, 1); rr.set(0, 0, 0.0f); mm.state().channels[0].pan = -1.0f;
        auto& f = mm.state().channels[0].fx; f.eqOn = true; f.compOn = true; f.midGain = 12; f.threshDb = -30; f.ratio = 8;
        engine::AudioEngine ee(mm, rr); ee.prepare(2400);
        std::vector<float> sn(2400, 0.3f), o0(2400), o1(2400);
        const float* ip[1] = {sn.data()}; float* op[2] = {o0.data(), o1.data()};
        ee.sync(); for (int k = 0; k < 3; ++k) ee.process(ip, op, 2400);
        f.eqOn = false; f.compOn = false; ee.sync(); ee.process(ip, op, 2400);
        CHECK(near(o0[2399], 0.3f, 1e-5f)); CHECK(ee.gainReductionDb(0) == 0.0f);
    }
    // Live add: a channel appears while the engine is running, the old one keeps working
    {
        mixer::Mixer m3(1, 1); mixer::RoutingMatrix r3(1, 1); r3.set(0, 0, 0.0f);
        engine::AudioEngine e3(m3, r3); e3.prepare(256);
        std::vector<float> a(256, 0.5f), b(256, 0.25f), o0(256), o1(256);
        const float* ip1[1] = {a.data()}; float* op[2] = {o0.data(), o1.data()};
        e3.sync(); for (int k = 0; k < 40; ++k) e3.process(ip1, op, 256);
        CHECK(near(o0[255], 0.5f * 0.7071f));
        m3.addChannel(); r3.addInput(); r3.set(1, 0, 0.0f); m3.state().channels[1].pan = -1.0f; e3.sync();
        const float* ip2[2] = {a.data(), b.data()};
        for (int k = 0; k < 40; ++k) e3.process(ip2, op, 256);
        CHECK(near(o0[255], 0.5f * 0.7071f + 0.25f));
        CHECK(near(o1[255], 0.5f * 0.7071f));
        m3.removeLastChannel(); r3.resize(1, 1); e3.sync();
        for (int k = 0; k < 40; ++k) e3.process(ip1, op, 256);
        CHECK(near(o0[255], 0.5f * 0.7071f));
    }
    // Bus reverb: tail continues after the input stops
    {
        mixer::Mixer m4(1, 1); mixer::RoutingMatrix r4(1, 1); r4.set(0, 0, 0.0f);
        m4.state().buses[0].reverbOn = true; m4.state().buses[0].reverb.mix = 1.0f; m4.state().buses[0].reverb.type = wm::engine::ReverbType::Hall;
        engine::AudioEngine e4(m4, r4); e4.prepare(512); e4.sync();
        std::vector<float> a(512, 0.5f), o0(512), o1(512);
        const float* ip[1] = {a.data()}; float* op[2] = {o0.data(), o1.data()};
        for (int k = 0; k < 10; ++k) e4.process(ip, op, 512);   // let the gain ramp settle, feed signal
        std::fill(a.begin(), a.end(), 0.0f);
        float tail = 0; for (int k = 0; k < 40; ++k) { e4.process(ip, op, 512); for (float v : o0) tail += v * v; }
        CHECK(tail > 1e-5f);
    }
    // Triple buffer under a real writer/reader race: every block read must be internally consistent.
    {
        struct P { int a = 0, b = 0, c = 0; };
        engine::TripleBuffer<P> tb; std::atomic<bool> stop{false}; std::atomic<int> bad{0}; int last = 0; bool backwards = false;
        std::thread w([&] { for (int i = 1; i <= 200000; ++i) { auto& s = tb.writeSlot(); s.a = i; s.b = i; s.c = i; tb.publish(); } stop = true; });
        while (!stop) { const P& p = tb.acquire(); if (p.a != p.b || p.b != p.c) ++bad; if (p.a < last) backwards = true; last = p.a; }
        w.join();
        CHECK(bad == 0 && !backwards);
    }
    // Stereo pair, width, M/S decode and FX send
    {
        mixer::Mixer m2(2, 2); mixer::RoutingMatrix r2(2, 2);
        engine::AudioEngine e2(m2, r2); e2.prepare(256);
        std::vector<std::vector<float>> in2(2, std::vector<float>(256)), out2(4, std::vector<float>(256));
        for (int i = 0; i < 256; ++i) { in2[0][size_t(i)] = 0.6f; in2[1][size_t(i)] = 0.2f; }   // mid 0.4, side 0.2
        m2.state().channels[0].stereoLinked = true; r2.set(0, 0, 0.0f);
        run(e2, in2, out2);
        CHECK(near(out2[0][255], 0.6f) && near(out2[1][255], 0.2f));            // pair: left and right pass straight
        m2.state().channels[0].width = 0.0f; run(e2, in2, out2);
        CHECK(near(out2[0][255], 0.4f) && near(out2[1][255], 0.4f));            // width 0 = mono
        m2.state().channels[0].width = 2.0f; run(e2, in2, out2);
        CHECK(near(out2[0][255], 0.8f) && near(out2[1][255], 0.0f));            // width 2 doubles the side
        m2.state().channels[0].width = 1.0f; m2.state().channels[0].midSide = true; run(e2, in2, out2);
        CHECK(near(out2[0][255], 0.8f) && near(out2[1][255], 0.4f));            // M/S decode: L=M+S, R=M-S
        m2.state().channels[0].midSide = false; m2.state().channels[0].pan = 1.0f; run(e2, in2, out2);
        CHECK(near(out2[0][255], 0.0f) && near(out2[1][255], 0.2f));            // balance full right keeps right only
        m2.state().channels[0].pan = 0; m2.state().channels[0].fxSendDb = 0.0f; run(e2, in2, out2);
        CHECK(near(out2[2][255], 0.6f) && near(out2[3][255], 0.2f));            // FX send reaches the last bus
        m2.state().channels[0].fxSendDb = -60.0f; run(e2, in2, out2);
        CHECK(near(out2[2][255], 0.0f, 1e-4f));
    }
    {   // the studio layout has more than 8 buses: audio must still play (the engine once went silent above 8)
        auto b = mixer::buildFromLayout(mixer::studioLayout());
        engine::AudioEngine e3(b.mixer, b.routing); e3.prepare(256);
        const int nc = int(b.mixer.state().channels.size()), nb = int(b.mixer.state().buses.size());
        CHECK(nb > 8);
        std::vector<std::vector<float>> in3(size_t(nc), std::vector<float>(256, 0.5f)), out3(size_t(nb) * 2, std::vector<float>(256));
        b.mixer.state().channels[size_t(nc) - 9].gainDb = 0.0f;   // COMPUTER playback, routed to Main
        run(e3, in3, out3);
        CHECK(std::fabs(out3[0][255]) > 0.001f || std::fabs(out3[1][255]) > 0.001f);
    }
    if (!fails) std::puts("engine_test: all passed");
    return fails ? 1 : 0;
}
