// MIDI link to the Pulse 16: opens one input and one output port, sends SysEx, hands received SysEx to the message thread.
// Everything the Pulse says goes through wm::pulse16::Session (src/backends/pulse16/).
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <functional>
#include <memory>
#include "../backends/pulse16/Pulse16Session.h"

namespace wmui {

class PulseLink : public wm::pulse16::Transport, public juce::MidiInputCallback {
public:
    std::function<void(const wm::pulse16::Bytes&)> onSysEx, onSent;   // onSent: a message we sent (for the SysEx log)   // called on the message thread, full F0 .. F7 message
    ~PulseLink() override { *alive = false; close(); }

    bool open(const juce::String& inId, const juce::String& outId) {
        close();
        in = juce::MidiInput::openDevice(inId, this);
        out = juce::MidiOutput::openDevice(outId);
        if (!in || !out) { close(); return false; }
        in->start();
        return true;
    }
    void close() { if (in) in->stop(); in.reset(); out.reset(); }
    bool isOpen() const { return in != nullptr && out != nullptr; }

    void sendSysEx(const wm::pulse16::Bytes& m) override {
        if (!out || m.size() < 3) return;
        if (onSent) onSent(m);
        out->sendMessageNow(juce::MidiMessage::createSysExMessage(m.data() + 1, int(m.size()) - 2));
    }
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m) override {
        if (!m.isSysEx()) return;
        auto bytes = wm::pulse16::withFraming(m.getSysExData(), m.getSysExDataSize());
        auto flag = alive;
        juce::MessageManager::callAsync([this, flag, bytes] { if (*flag && onSysEx) onSysEx(bytes); });
    }

private:
    std::unique_ptr<juce::MidiInput> in;
    std::unique_ptr<juce::MidiOutput> out;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

}  // namespace wmui
