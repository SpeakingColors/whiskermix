// Pulse 16 session model: connect handshake, live state, watchdog, meter polling and safe setters.
// Pure C++, no JUCE. The MIDI port is hidden behind Transport so tests (and a dry-run mode) need no hardware.
// Time is passed in (tick) so the logic is deterministic in tests.
#pragma once
#include <cstdint>
#include <functional>
#include "Pulse16Codec.h"

namespace wm::pulse16 {

struct Transport {
    virtual ~Transport() = default;
    virtual void sendSysEx(const Bytes& msg) = 0;
};

enum class Status { Disconnected, Probing, Connected, Lost, NoAnswer };

class Session {
public:
    static constexpr uint32_t kProbeTimeoutMs = 1500;   // no probe answer: NoAnswer
    static constexpr uint32_t kStateTimeoutMs = 3000;   // probe answered but the state blocks never arrived: NoAnswer
    static constexpr uint32_t kSilenceMs = 3000;        // connected but nothing heard (polls are answered every 0.22 s): Lost
    static constexpr uint32_t kPollMs = 220;            // same pace as RemoteFish

    explicit Session(Transport& t) : tr_(t) {}

    // Opens like RemoteFish: probe first. The probe answer carries the unit id; only then is 02 01 sent with that id
    // and the unit answers with its state blocks. We never send 01 04 01 (it would force headphone source Analog Out).
    void connect(uint32_t nowMs = 0) {
        now_ = nowMs; started_ = nowMs; lastRx_ = nowMs; lastPoll_ = nowMs;
        status_ = Status::Probing; haveGains_ = haveRouting_ = false; fr_ = Frames{}; st_ = Pulse16State{};
        tr_.sendSysEx(Frames::probe());
    }
    void disconnect() { status_ = Status::Disconnected; }

    // Feed every complete SysEx message received from the Pulse (F0 .. F7).
    void onSysEx(const Bytes& m) {
        if (m.size() < 12 || m[0] != 0xF0 || m[1] != 0x00 || m[2] != 0x11 || m[3] != 0x22) return;   // not a Ferrofish frame
        const bool probeAnswer = m.size() == 12 && m[9] == 0x03 && m[10] == 0x00;
        if (probeAnswer) {
            if (status_ == Status::Probing) {           // learn the id, ask for the state
                for (int i = 0; i < 5; ++i) fr_.id[size_t(i)] = m[size_t(4 + i)];
                status_ = Status::Connected; lastRx_ = now_; lastPoll_ = now_; started_ = now_;
                tr_.sendSysEx(fr_.hello());
            }
            return;
        }
        if (status_ != Status::Connected && status_ != Status::Lost) return;
        for (int i = 0; i < 5; ++i) if (m[size_t(4 + i)] != fr_.id[size_t(i)]) return;               // another unit on the same bus
        if (status_ == Status::Lost) status_ = Status::Connected;
        lastRx_ = now_;
        if (parseMeters(m, st_)) return;
        if (parseGains(m, st_)) { haveGains_ = true; return; }
        if (parseRouting(m, st_)) { haveRouting_ = true; return; }
        if (parseSettings(m, st_)) return;
        if (m.size() == 13 && m[9] == 0x01) {          // echo of a setting we (or the front panel) changed
            if (m[10] == 0x01 && m[11] <= 2) { st_.clock = Clock(m[11]); st_.clockKnown = true; }
            else if (m[10] == 0x02 && m[11] <= 8) { st_.rate = Rate(m[11]); st_.rateKnown = true; }
            else if (m[10] == 0x03 && m[11] <= 79) st_.hpVolumeVv = m[11];
            else if (m[10] == 0x04 && m[11] <= 7) st_.hpSource = m[11];
            else if (m[10] == 0x05) st_.hpChannelCode = m[11];
        } else if (m.size() == 13 && m[9] == 0x10) {   // fader echo: front panel or another editor moved it
            int idx = m[10], v = m[11];
            if (idx < 16 && v <= 28) st_.inputGainDb[idx] = v;
            else if (idx >= 16 && idx < 32 && v <= 28) st_.outputLevelDb[idx - 16] = v - 28;
        } else if (m.size() == 13 && m[9] == 0x11) {   // routing echo
            if (m[10] < 22 && m[11] < 14) st_.routeCol[m[10]] = m[11];
        }
    }

    // Call regularly (about every 100 ms) with a millisecond counter. Sends meter polls and runs the watchdog.
    void tick(uint32_t nowMs) {
        now_ = nowMs;
        if (status_ == Status::Probing && now_ - started_ > kProbeTimeoutMs) { status_ = Status::NoAnswer; return; }
        if (status_ != Status::Connected) return;
        if (!haveState() && now_ - started_ > kStateTimeoutMs) { status_ = Status::NoAnswer; return; }
        if (now_ - lastRx_ > kSilenceMs) { status_ = Status::Lost; return; }
        if (pollMeters_ && now_ - lastPoll_ >= kPollMs) { lastPoll_ = now_; tr_.sendSysEx(fr_.meterPoll()); }
    }
    // Polling doubles as the keep-alive the watchdog relies on. Turn it off only for tests.
    void setPolling(bool on) { pollMeters_ = on; }
    // Ask the unit to send its state blocks again (02 01).
    void requestState() { if (status_ == Status::Connected) { haveGains_ = haveRouting_ = false; started_ = now_; tr_.sendSysEx(fr_.hello()); } }

    Status status() const { return status_; }
    bool connected() const { return status_ == Status::Connected; }
    bool haveState() const { return haveGains_ && haveRouting_; }
    const Pulse16State& state() const { return st_; }
    const Id& unitId() const { return fr_.id; }

    // Setters send the command and update the local model. Values are clamped by the codec.
    void setInputGain(int ch, int dB) { if (!ok()) return; tr_.sendSysEx(fr_.inputGain(ch, dB)); st_.inputGainDb[ch - 1] = std::clamp(dB, 0, 28); }
    void setOutputLevel(int ch, int dB) { if (!ok()) return; tr_.sendSysEx(fr_.outputLevel(ch, dB)); st_.outputLevelDb[ch - 1] = std::clamp(dB, -28, 0); }
    void setRoute(int row, int col) { if (!ok() || row < 0 || row > 13 || col < 0 || col > 13) return; tr_.sendSysEx(fr_.route(row, col)); st_.routeCol[row] = uint8_t(col); }
    void setClock(Clock c) { if (!ok()) return; tr_.sendSysEx(fr_.clockSource(int(c))); st_.clock = c; st_.clockKnown = true; }
    // The Pulse screen can show 48 kHz while the unit runs another rate, and re-picking the shown value sends nothing in
    // RemoteFish. So always send the rate explicitly, even when the model already says it.
    void setRate(Rate r) { if (!ok()) return; tr_.sendSysEx(fr_.sampleRate(int(r))); st_.rate = r; st_.rateKnown = true; }
    void sendHeadphoneVolume(int vv) { if (!ok()) return; tr_.sendSysEx(fr_.headphoneVolume(vv)); st_.hpVolumeVv = std::clamp(vv, 0, 79); }
    void sendHeadphoneSource(HpSource s) { if (!ok()) return; tr_.sendSysEx(fr_.headphoneSource(int(s))); st_.hpSource = int(s); }
    void sendHeadphoneStereo(int firstCh) { if (!ok()) return; tr_.sendSysEx(fr_.headphoneStereo(firstCh)); st_.hpChannelCode = 0x40 + (firstCh - 1) / 2; }
    void sendHeadphoneMono(int ch) { if (!ok()) return; tr_.sendSysEx(fr_.headphoneMono(ch)); st_.hpChannelCode = ch - 1; }
    void sendBrightness(int v) { if (ok()) tr_.sendSysEx(fr_.displayBrightness(v)); }
    void sendKeyClick(bool on) { if (ok()) tr_.sendSysEx(fr_.keyClick(on)); }
    void savePresetSlot(int n) { if (ok()) tr_.sendSysEx(fr_.savePreset(n)); }   // overwrites the stored slot
    void loadPresetSlot(int n) { if (!ok()) return; tr_.sendSysEx(fr_.loadPreset(n)); haveGains_ = haveRouting_ = false; started_ = now_; } // unit replies with new state

    // Restore a saved setup: gains, levels, routing rows 0..13, clock, rate, headphones. Returns the number of frames sent.
    // The rate is sent explicitly. Clock is sent before the rate so the unit ends on the saved rate.
    int applyState(const Pulse16State& s) {
        if (!ok()) return 0;
        int n = 0;
        for (int i = 1; i <= 16; ++i) { setInputGain(i, s.inputGainDb[i - 1]); setOutputLevel(i, s.outputLevelDb[i - 1]); n += 2; }
        for (int r = 0; r < 14; ++r) { setRoute(r, s.routeCol[r]); ++n; }
        setClock(s.clock); setRate(s.rate); n += 2;
        sendHeadphoneSource(HpSource(s.hpSource)); sendHeadphoneVolume(s.hpVolumeVv); n += 2;
        int first = 1; bool stereo = true;
        if (decodeHeadphoneChannel(uint8_t(s.hpChannelCode), first, stereo)) { if (stereo) sendHeadphoneStereo(first); else sendHeadphoneMono(first); ++n; }
        return n;
    }

private:
    bool ok() const { return status_ == Status::Connected; }
    Transport& tr_;
    Frames fr_;
    Pulse16State st_;
    Status status_ = Status::Disconnected;
    bool haveGains_ = false, haveRouting_ = false, pollMeters_ = true;
    uint32_t now_ = 0, started_ = 0, lastRx_ = 0, lastPoll_ = 0;
};

} // namespace wm::pulse16
