// Software mixer core. Pure C++, no PipeWire or JUCE calls, so it is fully testable.
// Channels are mono inputs. Each bus is a stereo pair. Routing matrix (channel x bus) holds send gain in dB.
// Threading: the UI edits Mixer/RoutingMatrix, then calls sync() to hand a copy to the audio thread through a
// lock-free triple buffer. process() only reads that copy and never allocates or locks.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include "../mixer/MixerModel.h"
#include "../mixer/RoutingMatrix.h"
#include "Dsp.h"
#include "TripleBuffer.h"

namespace wm::engine {

inline float dbToLin(float db) { return db <= -119.0f ? 0.0f : std::pow(10.0f, db / 20.0f); }

class AudioEngine {
public:
    AudioEngine(mixer::Mixer& m, mixer::RoutingMatrix& r, float sampleRate = 48000.0f) : mx_(m), rt_(r) {
        setSampleRate(sampleRate);
        prepare(4096);
    }
    void setSampleRate(float sr) { sr_ = sr; coef_ = 1.0f - std::exp(-1.0f / (0.005f * sr)); }

    // UI thread. Call while audio is stopped (device start), and whenever channels or buses are added.
    void prepare(int maxFrames) {
        maxFrames_ = maxFrames;
        for (int i = 0; i < 3; ++i) { snaps_.all(i).state = mx_.state(); snaps_.all(i).routing = rt_; }
        size_t nc = mx_.state().channels.size(), nb = mx_.state().buses.size();
        cur_.assign(nc * nb * 2, 0.0f);
        fx_.assign(nc, ChannelFx{});
        buf_.assign(nc, std::vector<float>(size_t(maxFrames)));
        peakIn_.reset(new std::atomic<float>[nc]); gr_.reset(new std::atomic<float>[nc]);
        peakOut_.reset(new std::atomic<float>[nb * 2]);
        for (size_t i = 0; i < nc; ++i) { peakIn_[i] = 0; gr_[i] = 0; }
        for (size_t i = 0; i < nb * 2; ++i) peakOut_[i] = 0;
        nc_ = nc; nb_ = nb;
    }
    // UI thread: publish the current mixer state to the audio thread (cheap; call after edits, e.g. every 16 ms).
    void sync() {
        auto& s = snaps_.writeSlot();
        s.state = mx_.state(); s.routing = rt_;
        snaps_.publish();
    }

    // Audio thread. in[c] = nframes of channel c. out[2*b], out[2*b+1] = bus b left and right.
    void process(const float* const* in, float* const* out, int n) {
        const Snap& sn = snaps_.acquire();
        const auto& st = sn.state; const auto& rt = sn.routing;
        size_t nc = nc_, nb = nb_;
        if (st.channels.size() != nc || st.buses.size() != nb || n > maxFrames_) {   // setup changed: silence until prepare()
            for (size_t o = 0; o < nb * 2; ++o) std::fill(out[o], out[o] + n, 0.0f);
            return;
        }
        NoDenormals nd;
        for (size_t o = 0; o < nb * 2; ++o) std::fill(out[o], out[o] + n, 0.0f);
        bool anySolo = false;
        for (const auto& c : st.channels) anySolo = anySolo || c.solo;
        for (size_t c = 0; c < nc; ++c) {
            const auto& ch = st.channels[c];
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max(pk, std::fabs(in[c][i]));
            peakIn_[c].store(pk, std::memory_order_relaxed);
            const float* src = in[c];
            if (ch.fx.eqOn || ch.fx.compOn) {
                fx_[c].process(ch.fx, sr_, in[c], buf_[c].data(), n);
                src = buf_[c].data();
                gr_[c].store(fx_[c].gainReductionDb(), std::memory_order_relaxed);
            } else gr_[c].store(0.0f, std::memory_order_relaxed);
            float gdb = (ch.mute || (anySolo && !ch.solo)) ? -120.0f : ch.gainDb;
            float strip = dbToLin(gdb) * (ch.phaseL ? -1.0f : 1.0f);
            float pl = std::cos((ch.pan + 1.0f) * 0.78539816f), pr = std::sin((ch.pan + 1.0f) * 0.78539816f);  // constant power
            for (size_t b = 0; b < nb; ++b) {
                float send = rt.routed(int(c), int(b)) ? dbToLin(rt.get(int(c), int(b))) : 0.0f;
                float tl = strip * send * pl, tr = strip * send * pr;
                float& gl = cur_[(c * nb + b) * 2];
                float& gr = cur_[(c * nb + b) * 2 + 1];
                float *ol = out[b * 2], *orr = out[b * 2 + 1];
                for (int i = 0; i < n; ++i) {
                    gl += (tl - gl) * coef_;
                    gr += (tr - gr) * coef_;
                    ol[i] += src[i] * gl;
                    orr[i] += src[i] * gr;
                }
            }
        }
        for (size_t b = 0; b < nb; ++b) {
            const auto& bus = st.buses[b];
            float v = dbToLin(bus.volumeDb + (bus.dim ? digiface::kDimDb : 0.0f));
            for (size_t k = 0; k < 2; ++k) {
                float* o = out[b * 2 + k];
                float pk = 0;
                for (int i = 0; i < n; ++i) { o[i] *= v; pk = std::max(pk, std::fabs(o[i])); }
                peakOut_[b * 2 + k].store(pk, std::memory_order_relaxed);
            }
        }
    }
    // UI thread readers (atomics).
    float gainReductionDb(int c) const { return gr_[size_t(c)].load(std::memory_order_relaxed); }
    float inputPeakDb(int c) const { return toDb(peakIn_[size_t(c)].load(std::memory_order_relaxed)); }
    float outputPeakDb(int o) const { return toDb(peakOut_[size_t(o)].load(std::memory_order_relaxed)); }

private:
    struct Snap { mixer::State state; mixer::RoutingMatrix routing; };
    static float toDb(float v) { return v < 1e-6f ? -120.0f : 20.0f * std::log10(v); }
    mixer::Mixer& mx_;
    mixer::RoutingMatrix& rt_;
    TripleBuffer<Snap> snaps_;
    float coef_ = 0.01f, sr_ = 48000.0f;
    int maxFrames_ = 4096;
    size_t nc_ = 0, nb_ = 0;
    std::vector<float> cur_;
    std::vector<ChannelFx> fx_;
    std::vector<std::vector<float>> buf_;
    std::unique_ptr<std::atomic<float>[]> peakIn_, peakOut_, gr_;
};

}  // namespace wm::engine
