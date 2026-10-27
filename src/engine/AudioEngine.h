// Software mixer core. Pure C++, no PipeWire or JUCE calls, so it is fully testable.
// Channels are mono inputs. Each bus is a stereo pair. Routing matrix (channel x bus) holds send gain in dB.
// Threading: the UI edits Mixer/RoutingMatrix, then calls sync() to hand a copy to the audio thread through a
// lock-free triple buffer. process() only reads that copy and never allocates or locks.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include "../mixer/MixerModel.h"
#include "../mixer/RoutingMatrix.h"
#include "Dsp.h"
#include "Reverb.h"
#include "Chain.h"
#include "TripleBuffer.h"

namespace wm::engine {

constexpr size_t kMaxChannels = 64, kMaxBuses = 8;   // everything is preallocated for these, so channels can be added live

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
        cur_.assign(kMaxChannels * kMaxBuses * 2, 0.0f);
        curSend_.assign(kMaxChannels * 2, 0.0f); wl_.assign(size_t(maxFrames), 0.0f); wr_.assign(size_t(maxFrames), 0.0f); srcp_.assign(kMaxChannels, nullptr);
        fx_.assign(kMaxChannels, ChannelFx{}); chain_.assign(kMaxChannels, ChannelChain{}); for (auto& c : chain_) c.prepare(sr_);
        fxIn_.reset(new std::atomic<float>[kMaxChannels * kFxKinds]); fxOut_.reset(new std::atomic<float>[kMaxChannels * kFxKinds]);
        for (size_t i = 0; i < kMaxChannels * kFxKinds; ++i) { fxIn_[i] = 0; fxOut_[i] = 0; }
        buf_.assign(kMaxChannels, std::vector<float>(size_t(maxFrames)));
        peakIn_.reset(new std::atomic<float>[kMaxChannels]); gr_.reset(new std::atomic<float>[kMaxChannels]);
        peakOut_.reset(new std::atomic<float>[kMaxBuses * 2]);
        for (size_t i = 0; i < kMaxChannels; ++i) { peakIn_[i] = 0; gr_[i] = 0; }
        for (size_t i = 0; i < kMaxBuses * 2; ++i) peakOut_[i] = 0;
        rev_.assign(kMaxBuses, ReverbUnit{}); dly_.assign(kMaxBuses, ShimmerDelay{});
        for (auto& r : rev_) r.prepare(sr_);
        for (auto& d : dly_) d.prepare(sr_);
        nc_ = 0; nb_ = 0;
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
        size_t nc = st.channels.size(), nb = st.buses.size();
        if (nc > kMaxChannels || nb > kMaxBuses || rt.inputs() != int(nc) || rt.outputs() != int(nb) || n > maxFrames_) {
            for (size_t o = 0; o < nb * 2 && o < kMaxBuses * 2; ++o) std::fill(out[o], out[o] + n, 0.0f);   // inconsistent: silence
            return;
        }
        if (nc != nc_ || nb != nb_) {   // channels or buses added or removed live: reset the DSP state of the ones that appeared
            for (size_t c = nc_; c < nc; ++c) { fx_[c] = ChannelFx{}; }
            for (size_t c = 0; c < kMaxChannels; ++c) for (size_t b = 0; b < kMaxBuses; ++b)
                if (c >= nc_ || b >= nb_) { cur_[(c * kMaxBuses + b) * 2] = 0; cur_[(c * kMaxBuses + b) * 2 + 1] = 0; }
            nc_ = nc; nb_ = nb;
        }
        NoDenormals nd;
        for (size_t o = 0; o < nb * 2; ++o) std::fill(out[o], out[o] + n, 0.0f);
        bool anySolo = false;
        for (const auto& c : st.channels) anySolo = anySolo || c.solo;
        // Pass 1: peaks and channel EQ/compressor.
        for (size_t c = 0; c < nc; ++c) {
            const auto& ch = st.channels[c];
            float pk = 0;
            for (int i = 0; i < n; ++i) pk = std::max(pk, std::fabs(in[c][i]));
            peakIn_[c].store(pk, std::memory_order_relaxed);
            srcp_[c] = in[c];
            bool chainOn = false; for (int k = 0; k < kFxKinds; ++k) chainOn = chainOn || ch.chain.s[k].on;
            if (ch.fx.eqOn || ch.fx.compOn || chainOn) {
                float* b = buf_[c].data();
                if (ch.fx.eqOn || ch.fx.compOn) fx_[c].process(ch.fx, sr_, in[c], b, n); else std::memcpy(b, in[c], size_t(n) * sizeof(float));
                float ip[kFxKinds], op[kFxKinds];
                chain_[c].process(ch.chain, b, n, ip, op);
                for (int k = 0; k < kFxKinds; ++k) { fxIn_[c * kFxKinds + size_t(k)].store(ip[k], std::memory_order_relaxed); fxOut_[c * kFxKinds + size_t(k)].store(op[k], std::memory_order_relaxed); }
                srcp_[c] = b;
                gr_[c].store(fx_[c].gainReductionDb(), std::memory_order_relaxed);
            } else {   // everything off: bypass, the signal is not touched and all state is cleared
                fx_[c].process(ch.fx, sr_, in[c], buf_[c].data(), 0); chain_[c].process(ch.chain, buf_[c].data(), 0);
                for (int k = 0; k < kFxKinds; ++k) { fxIn_[c * kFxKinds + size_t(k)].store(0, std::memory_order_relaxed); fxOut_[c * kFxKinds + size_t(k)].store(0, std::memory_order_relaxed); }
                gr_[c].store(0.0f, std::memory_order_relaxed);
            }
        }
        // M/S FX on linked pairs (left channel's chain holds the setting)
        for (size_t c = 0; c + 1 < nc; ++c) {
            const auto& ch = st.channels[c];
            if (!ch.stereoLinked) continue;
            const auto& ms = ch.chain.s[kMidSide];
            if (ms.on) {
                for (size_t q : {c, c + 1}) if (srcp_[q] == in[q]) { std::memcpy(buf_[q].data(), in[q], size_t(n) * sizeof(float)); srcp_[q] = buf_[q].data(); }
                chain_[c].processMidSide(ms, buf_[c].data(), buf_[c + 1].data(), n);
                fxIn_[c * kFxKinds + kMidSide].store(1.0f, std::memory_order_relaxed); fxOut_[c * kFxKinds + kMidSide].store(1.0f, std::memory_order_relaxed);
            } else chain_[c].processMidSide(ms, nullptr, nullptr, 0);
            ++c;
        }
        // Pass 2: routing. A channel with "stereoLinked" is the left of a pair with the next channel (the right one
        // follows the left one's fader, mute, solo, pan (balance), width and M/S settings).
        bool skip = false;
        for (size_t c = 0; c < nc; ++c) {
            if (skip) { skip = false; continue; }
            const auto& ch = st.channels[c];
            const bool pair = ch.stereoLinked && c + 1 < nc;
            skip = pair;
            float gdb = (ch.mute || (anySolo && !ch.solo)) ? -120.0f : ch.gainDb;
            float lin = dbToLin(gdb);
            const float *sl = srcp_[c], *srr = nullptr;
            float pl, pr, sgnL = ch.phaseL ? -1.0f : 1.0f, sgnR = 1.0f;
            if (pair) {
                const float *l0 = srcp_[c], *r0 = srcp_[c + 1];
                const float w = std::max(0.0f, std::min(2.0f, ch.width));
                for (int i = 0; i < n; ++i) {
                    float l = l0[i], r = r0[i];
                    if (ch.midSide) { float m = l, sd = r; l = m + sd; r = m - sd; }   // input is Mid/Side: decode to L/R
                    float m = (l + r) * 0.5f, sd = (l - r) * 0.5f * w;
                    wl_[size_t(i)] = m + sd; wr_[size_t(i)] = m - sd;
                }
                sl = wl_.data(); srr = wr_.data();
                pl = ch.pan > 0 ? 1.0f - ch.pan : 1.0f; pr = ch.pan < 0 ? 1.0f + ch.pan : 1.0f;   // balance
                sgnR = ch.phaseR ? -1.0f : 1.0f;
            } else {
                pl = std::cos((ch.pan + 1.0f) * 0.78539816f); pr = std::sin((ch.pan + 1.0f) * 0.78539816f);   // constant power
                sgnR = sgnL;
            }
            for (size_t b = 0; b < nb; ++b) {
                float send = rt.routed(int(c), int(b)) ? dbToLin(rt.get(int(c), int(b))) : 0.0f;
                float tl = lin * send * pl * sgnL, tr = lin * send * pr * sgnR;
                float& gl = cur_[(c * kMaxBuses + b) * 2];
                float& gr = cur_[(c * kMaxBuses + b) * 2 + 1];
                float *ol = out[b * 2], *orr = out[b * 2 + 1];
                for (int i = 0; i < n; ++i) {
                    gl += (tl - gl) * coef_;
                    gr += (tr - gr) * coef_;
                    ol[i] += sl[i] * gl;
                    orr[i] += (srr ? srr[i] : sl[i]) * gr;
                }
            }
            if (nb > 1) {   // FX send: goes to the last bus (the FX return bus), -60 dB = off
                float fs = ch.fxSendDb > -59.0f ? dbToLin(ch.fxSendDb) : 0.0f;
                float tl = lin * fs * pl * sgnL, tr = lin * fs * pr * sgnR;
                float& gl = curSend_[c * 2]; float& gr = curSend_[c * 2 + 1];
                float *ol = out[(nb - 1) * 2], *orr = out[(nb - 1) * 2 + 1];
                if (tl != 0.0f || tr != 0.0f || gl != 0.0f || gr != 0.0f)
                    for (int i = 0; i < n; ++i) {
                        gl += (tl - gl) * coef_; gr += (tr - gr) * coef_;
                        ol[i] += sl[i] * gl; orr[i] += (srr ? srr[i] : sl[i]) * gr;
                    }
            }
        }
        for (size_t b = 0; b < nb; ++b) {
            const auto& bus = st.buses[b];
            float v = dbToLin(bus.volumeDb + (bus.dim ? digiface::kDimDb : 0.0f));
            if (bus.delayOn || bus.reverbOn) {
                float *L = out[b * 2], *R = out[b * 2 + 1];
                for (int i = 0; i < n; ++i) {
                    float l = L[i], r = R[i];
                    if (bus.delayOn) dly_[b].process(l, r, l, r, bus.delay);
                    if (bus.reverbOn) rev_[b].process(l, r, l, r, bus.reverb);
                    L[i] = l; R[i] = r;
                }
            }
            for (size_t k = 0; k < 2; ++k) {
                float* o = out[b * 2 + k];
                float pk = 0;
                for (int i = 0; i < n; ++i) { o[i] *= v; pk = std::max(pk, std::fabs(o[i])); }
                peakOut_[b * 2 + k].store(pk, std::memory_order_relaxed);
            }
        }
    }
    // UI thread readers (atomics).
    // Per-FX level before and after (linear peak of the last block); 0 = idle, which is what an Off FX reports.
    float fxInPeak(int c, int kind) const { return fxIn_[size_t(c) * kFxKinds + size_t(kind)].load(std::memory_order_relaxed); }
    float fxOutPeak(int c, int kind) const { return fxOut_[size_t(c) * kFxKinds + size_t(kind)].load(std::memory_order_relaxed); }
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
    size_t nc_ = 0, nb_ = 0;   // counts the DSP state is currently valid for (audio thread only)
    std::vector<float> cur_, curSend_, wl_, wr_;
    std::vector<const float*> srcp_;
    std::vector<ChannelFx> fx_;
    std::vector<ChannelChain> chain_;
    std::unique_ptr<std::atomic<float>[]> fxIn_, fxOut_;
    std::vector<ReverbUnit> rev_;
    std::vector<ShimmerDelay> dly_;
    std::vector<std::vector<float>> buf_;
    std::unique_ptr<std::atomic<float>[]> peakIn_, peakOut_, gr_;
};

}  // namespace wm::engine
