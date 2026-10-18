// Lock-free hand-off from one writer thread (UI) to one reader thread (audio). The reader never waits or allocates.
#pragma once
#include <atomic>
namespace wm::engine {
template <class T>
class TripleBuffer {
public:
    T& writeSlot() { return buf_[w_]; }
    void publish() { int old = mid_.exchange(w_ | kDirty, std::memory_order_acq_rel); w_ = old & 3; }
    const T& acquire() {   // reader side
        if (mid_.load(std::memory_order_acquire) & kDirty) { int old = mid_.exchange(r_, std::memory_order_acq_rel); r_ = old & 3; }
        return buf_[r_];
    }
    T& all(int i) { return buf_[i]; }   // for initial setup before any thread starts
private:
    static constexpr int kDirty = 8;
    T buf_[3];
    int w_ = 0, r_ = 2;
    std::atomic<int> mid_{1};
};
}
