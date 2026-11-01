// Virtual playback channels: audio that other programs play into WhiskerMix (the "Computer" strips).
// One lock-free single-producer single-consumer ring per channel. The producer is whatever delivers the audio
// (a virtual audio device, loopback capture, a test generator); the consumer is the audio callback.
// Playback strip with hardware pair index h reads virtual channel h (like TotalMix software playback channels).
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace wm::engine {

constexpr size_t kVirtualChannels = 64;

class VirtualFeed {
public:
    VirtualFeed() : rings_(kVirtualChannels) { for (auto& r : rings_) r.buf.assign(kCap, 0.0f); }
    // Producer thread. Drops the oldest audio when the reader is far behind, so latency stays bounded.
    void write(size_t ch, const float* src, int n) {
        if (ch >= kVirtualChannels || n <= 0) return;
        auto& r = rings_[ch];
        uint64_t w = r.w.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) r.buf[size_t((w + uint64_t(i)) & kMask)] = src[i];
        r.w.store(w + uint64_t(n), std::memory_order_release);
        r.lastWrite.store(w + uint64_t(n), std::memory_order_relaxed);
    }
    // Consumer (audio) thread. Fills dst with n frames; silence when the producer has nothing. Returns true when real audio was read.
    bool read(size_t ch, float* dst, int n) {
        if (ch >= kVirtualChannels) { std::fill(dst, dst + n, 0.0f); return false; }
        auto& r = rings_[ch];
        const uint64_t w = r.w.load(std::memory_order_acquire);
        uint64_t rd = r.r;
        if (w == 0) { std::fill(dst, dst + n, 0.0f); return false; }
        uint64_t avail = w - rd;
        if (avail > kMaxLag) { rd = w - kKeep; avail = kKeep; }          // reader far behind: skip ahead, bounded latency
        const int take = int(std::min<uint64_t>(avail, uint64_t(n)));
        for (int i = 0; i < take; ++i) dst[i] = r.buf[size_t((rd + uint64_t(i)) & kMask)];
        std::fill(dst + take, dst + n, 0.0f);                              // underrun: pad with silence
        r.r = rd + uint64_t(take);
        return take > 0;
    }
private:
    static constexpr size_t kCap = 1u << 15;       // 32768 frames per channel
    static constexpr uint64_t kMask = kCap - 1, kMaxLag = 16384, kKeep = 2048;
    struct Ring { std::vector<float> buf; std::atomic<uint64_t> w{0}, lastWrite{0}; uint64_t r = 0; };
    std::vector<Ring> rings_;
};

}  // namespace wm::engine
