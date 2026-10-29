// Links the mixer model to the Digiface USB hardware mixer: a 20 ms timer (the vendor mixer's own write pace)
// runs Sync, which writes only what changed. Without the device (not plugged, no permission, Windows/macOS test
// build) it runs in dry-run mode against a RecordingTransport and keeps trying to open the device every 3 s.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <string>
#include "../backends/digiface/DigifaceSync.h"
#ifndef WM_JUCE_AUDIO
#include "../backends/digiface/LibusbTransport.h"
#endif

namespace wmui {

class DigifaceLink : private juce::Timer {
public:
    // mainA / mainB: bus indices used as Main (control room) and Speaker B.
    DigifaceLink(wm::mixer::Mixer& m, int mainA, int mainB) : mx(m), sync(dry) {
        sync.setMainBuses(mainA, mainB);
        tryOpen();
        startTimer(20);
    }
    ~DigifaceLink() override { stopTimer(); }

    bool live() const { return liveTr != nullptr; }
    std::string status() const { return live() ? "Digiface USB: connected" : "Digiface USB: dry run (" + reason + ")"; }
    void setSpeakerB(bool on) { sync.setSpeakerB(on); }
    void setMainMono(bool on) { sync.setMainMono(on); }
    // Send the whole state again (after a reconnect or a workspace load).
    void resend() { sync.invalidate(); }
    int writesLastSecond() const { return perSecond; }

private:
    void tryOpen() {
#ifndef WM_JUCE_AUDIO
        auto t = std::make_unique<wm::digiface::LibusbTransport>();
        std::string err;
        if (t->open(&err)) {
            liveTr = std::move(t);
            sync.setTransport(*liveTr);
            reason.clear();
            return;
        }
        reason = err.empty() ? "device not found" : err;
#else
        reason = "no USB access in this build";
#endif
    }
    void timerCallback() override {
        ++tick;
        if (!live() && tick % 150 == 0) {   // every 3 s
#ifndef WM_JUCE_AUDIO
            tryOpen();
#endif
        }
        {   // Mono and Speaker B come from the control room buses (first = Main, second = Speaker B)
            auto& bs = mx.state().buses; int a = -1, b2 = -1;
            for (size_t i = 0; i < bs.size(); ++i) if (bs[i].controlRoom) { if (a < 0) a = int(i); else if (b2 < 0) b2 = int(i); }
            sync.setMainMono(a >= 0 && bs[size_t(a)].mono); sync.setSpeakerB(b2 >= 0 && bs[size_t(b2)].solo);
        }
        const int n = sync.sync(mx);
        count += n;
        if (tick % 50 == 0) { perSecond = count; count = 0; }
    }
    wm::mixer::Mixer& mx;
    wm::digiface::RecordingTransport dry;
#ifndef WM_JUCE_AUDIO
    std::unique_ptr<wm::digiface::LibusbTransport> liveTr;
#else
    std::unique_ptr<wm::digiface::Transport> liveTr;
#endif
    wm::digiface::Sync sync;
    std::string reason;
    int tick = 0, count = 0, perSecond = 0;
};

}  // namespace wmui
