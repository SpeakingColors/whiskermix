// Routing matrix: gain in dB from every input to every output, with "no route" as -inf.
#pragma once
#include <algorithm>
#include <vector>

namespace wm::mixer {

constexpr float kNoRoute = -120.0f;

class RoutingMatrix {
public:
    RoutingMatrix(int inputs = 0, int outputs = 0)
        : in_(inputs), out_(outputs), g_(size_t(inputs) * size_t(outputs), kNoRoute) {}
    int inputs() const { return in_; }
    int outputs() const { return out_; }
    float get(int i, int o) const { return g_.at(idx(i, o)); }
    void set(int i, int o, float db) { g_.at(idx(i, o)) = db; }
    bool routed(int i, int o) const { return get(i, o) > kNoRoute; }
    void toggle(int i, int o, float db = 0.0f) { set(i, o, routed(i, o) ? kNoRoute : db); }
    void clear() { std::fill(g_.begin(), g_.end(), kNoRoute); }
    // Add a row or column (the window grows as channels are added).
    void addInput() { g_.insert(g_.end(), size_t(out_), kNoRoute); ++in_; }
    void addOutput() {
        std::vector<float> n;
        for (int i = 0; i < in_; ++i) {
            for (int o = 0; o < out_; ++o) n.push_back(g_[idx(i, o)]);
            n.push_back(kNoRoute);
        }
        g_ = n; ++out_;
    }
    // Grow or shrink, keeping existing routes (used after recalling a snapshot with a different channel count).
    void resize(int inputs, int outputs) {
        RoutingMatrix n(inputs, outputs);
        for (int i = 0; i < std::min(in_, inputs); ++i) for (int o = 0; o < std::min(out_, outputs); ++o) n.set(i, o, get(i, o));
        *this = n;
    }
    const std::vector<float>& data() const { return g_; }

private:
    size_t idx(int i, int o) const { return size_t(i) * size_t(out_) + size_t(o); }
    int in_, out_;
    std::vector<float> g_;
};

}  // namespace wm::mixer
