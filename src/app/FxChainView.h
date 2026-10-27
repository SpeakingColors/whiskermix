// FX panel of one channel (the mockup's panel that opens beside the channel). Twelve FX: EQ, Dynamics and the ten chain FX
// in engine/Chain.h. RULE: Off = bypass. An FX that is off shows idle meters ("off"), dimmed knobs, and the engine does not
// run it at all. Included by MixerView.h (needs Meter and col::).
#pragma once
// (Included inside namespace wmui, after Meter and col:: are defined.)
class FxChainPanel : public juce::Component, private juce::Timer {
public:
    static constexpr int kEq = 100, kDyn = 101;   // ids for the two older FX; chain FX use their engine::FxKind value
    std::function<void()> onClose;
    FxChainPanel(wm::mixer::Mixer& m, wm::engine::AudioEngine& e) : mx(m), eng(e) {
        addAndMakeVisible(typeBox); addAndMakeVisible(onBtn); addAndMakeVisible(close); addAndMakeVisible(meterIn); addAndMakeVisible(meterOut);
        int id = 1; for (int k : order()) { typeBox.addItem(nameOf(k), id); ids.push_back(k); ++id; }
        typeBox.setSelectedId(1, juce::dontSendNotification);
        typeBox.onChange = [this] { kind = ids[size_t(typeBox.getSelectedId() - 1)]; rebuildKnobs(); refresh(); };
        onBtn.setClickingTogglesState(false); onBtn.setTooltip("FX on/off for this channel. Off = bypassed: the sound passes untouched and this FX does not run.");
        onBtn.onClick = [this] { setOn(kind, !isOn(kind)); refresh(); };
        close.setButtonText(juce::CharPointer_UTF8("\xC3\x97")); close.setTooltip("Close the FX panel (the chain keeps running)"); close.onClick = [this] { if (onClose) onClose(); };
        for (int i = 0; i < 4; ++i) { knobs[i].setSliderStyle(juce::Slider::RotaryVerticalDrag); knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16); addAndMakeVisible(knobs[i]); addAndMakeVisible(labels[i]); labels[i].setJustificationType(juce::Justification::centred); labels[i].setColour(juce::Label::textColourId, col::dim); }
        for (size_t i = 0; i < ids.size(); ++i) {   // chain row: lit = on, click selects
            auto* b = chips.add(new juce::TextButton(nameOf(ids[i]))); b->setTooltip("Select " + nameOf(ids[i]) + " (lit = on)");
            b->onClick = [this, i] { typeBox.setSelectedId(int(i) + 1, juce::sendNotification); }; addAndMakeVisible(b);
        }
        rebuildKnobs(); startTimerHz(30);
    }
    void setChannel(int c) { ch = c; rebuildKnobs(); refresh(); }
    void paint(juce::Graphics& g) override {
        g.setColour(col::panel); g.fillRoundedRectangle(getLocalBounds().toFloat(), 6);
        g.setColour(col::violet); g.setFont(13.0f);
        if (ch >= 0 && ch < int(mx.state().channels.size())) g.drawText("FX  " + juce::String(mx.state().channels[size_t(ch)].name), 10, 6, getWidth() - 60, 20, juce::Justification::centredLeft);
        g.setColour(col::dim); g.setFont(10.0f);
        g.drawText("IN", meterIn.getX() - 6, meterIn.getY() - 14, 24, 12, juce::Justification::centred); g.drawText("OUT", meterOut.getX() - 8, meterOut.getY() - 14, 28, 12, juce::Justification::centred);
        g.drawText(inTxt, meterIn.getX() - 8, meterIn.getBottom() + 2, 40, 12, juce::Justification::centred); g.drawText(outTxt, meterOut.getX() - 8, meterOut.getBottom() + 2, 40, 12, juce::Justification::centred);
        if (kind == wm::engine::kMidSide) { g.setColour(col::dim); g.drawText("M/S works on a linked pair", 10, getHeight() - 76, getWidth() - 20, 14, juce::Justification::centred); }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8); r.removeFromTop(24);
        close.setBounds(getWidth() - 30, 6, 22, 20);
        auto top = r.removeFromTop(26); typeBox.setBounds(top.removeFromLeft(top.getWidth() - 64)); onBtn.setBounds(top.reduced(2, 0));
        r.removeFromTop(18);
        auto mt = r.removeFromRight(70); meterIn.setBounds(mt.removeFromLeft(24).withTrimmedBottom(16)); mt.removeFromLeft(6); meterOut.setBounds(mt.removeFromLeft(24).withTrimmedBottom(16));
        auto chipArea = r.removeFromBottom(64); r.removeFromBottom(4);
        int cw = chipArea.getWidth() / 6; for (int i = 0; i < chips.size(); ++i) chips[i]->setBounds(chipArea.getX() + (i % 6) * cw, chipArea.getY() + (i / 6) * 30, cw - 3, 26);
        int kw = r.getWidth() / 2, kh = r.getHeight() / 2;
        for (int i = 0; i < 4; ++i) { auto cell = juce::Rectangle<int>(r.getX() + (i % 2) * kw, r.getY() + (i / 2) * kh, kw, kh); labels[i].setBounds(cell.removeFromBottom(14)); knobs[i].setBounds(cell); }
    }
private:
    static std::vector<int> order() { std::vector<int> v = {kEq, kDyn}; for (int k = 0; k < wm::engine::kFxKinds; ++k) v.push_back(k); return v; }
    static juce::String nameOf(int k) { return k == kEq ? "EQ" : k == kDyn ? "Dynamics" : wm::engine::fxSpec(k).name; }
    wm::mixer::Channel* cur() { return ch >= 0 && ch < int(mx.state().channels.size()) ? &mx.state().channels[size_t(ch)] : nullptr; }
    bool isOn(int k) { auto* c = cur(); if (!c) return false; return k == kEq ? c->fx.eqOn : k == kDyn ? c->fx.compOn : c->chain.s[k].on; }
    void setOn(int k, bool v) { auto* c = cur(); if (!c) return; if (k == kEq) c->fx.eqOn = v; else if (k == kDyn) c->fx.compOn = v; else c->chain.s[k].on = v; }
    // value pointer + range for knob i of the shown FX
    struct KnobRef { float* v; double lo, hi, def; juce::String name, unit; };
    KnobRef knobRef(int i) {
        auto* c = cur(); static float dummy = 0; if (!c) return {&dummy, 0, 1, 0, "", ""};
        if (kind == kEq) { static const char* n[4] = {"Low cut", "Low", "Mid", "High"}; float* v[4] = {&c->fx.lowCutHz, &c->fx.lowGain, &c->fx.midGain, &c->fx.highGain}; double lo[4] = {0, -12, -12, -12}, hi[4] = {400, 12, 12, 12};
            return {v[i], lo[i], hi[i], 0, n[i], i == 0 ? "Hz" : "dB"}; }
        if (kind == kDyn) { static const char* n[4] = {"Thresh", "Ratio", "Attack", "Gain"}; float* v[4] = {&c->fx.threshDb, &c->fx.ratio, &c->fx.attackMs, &c->fx.makeupDb}; double lo[4] = {-48, 1, 1, 0}, hi[4] = {0, 10, 100, 12}, df[4] = {-18, 3, 10, 0}; const char* u[4] = {"dB", ":1", "ms", "dB"};
            return {v[i], lo[i], hi[i], df[i], n[i], u[i]}; }
        const auto& k = wm::engine::fxSpec(kind).k[i]; return {&c->chain.s[kind].p[i], k.lo, k.hi, k.def, k.name, k.unit};
    }
    void rebuildKnobs() {
        for (int i = 0; i < 4; ++i) {
            auto kr = knobRef(i); knobs[i].onValueChange = nullptr;
            knobs[i].setRange(kr.lo, kr.hi, (kr.hi - kr.lo) / 400.0); knobs[i].setDoubleClickReturnValue(true, kr.def);
            knobs[i].setValue(*kr.v, juce::dontSendNotification); knobs[i].setTextValueSuffix(" " + kr.unit); labels[i].setText(kr.name, juce::dontSendNotification);
            knobs[i].onValueChange = [this, i] { auto k = knobRef(i); *k.v = float(knobs[size_t(i)].getValue()); };   // editing never switches the FX on
        }
    }
    void refresh() {
        bool on = isOn(kind); onBtn.setButtonText(on ? "On" : "Off"); onBtn.setColour(juce::TextButton::buttonColourId, on ? col::teal.darker(0.3f) : col::bg);
        for (int i = 0; i < 4; ++i) knobs[i].setAlpha(on ? 1.0f : 0.45f);
        for (int i = 0; i < chips.size(); ++i) { bool o = isOn(ids[size_t(i)]); chips[i]->setColour(juce::TextButton::buttonColourId, o ? col::teal.darker(0.4f) : col::panel); chips[i]->setColour(juce::TextButton::textColourOffId, o ? col::text : col::dim); }
        repaint();
    }
    static float toDb(float lin) { return lin > 1.0e-6f ? 20.0f * std::log10(lin) : -60.0f; }
    void timerCallback() override {
        if (!isShowing() || ch < 0) return;
        bool on = isOn(kind); float dIn = -60, dOut = -60; inTxt = outTxt = "off";
        if (on && kind >= 0 && kind < wm::engine::kFxKinds && kind != wm::engine::kMidSide) { dIn = toDb(eng.fxInPeak(ch, kind)); dOut = toDb(eng.fxOutPeak(ch, kind)); }
        else if (on && kind == kEq) { dIn = dOut = eng.inputPeakDb(ch); }
        else if (on && kind == kDyn) { dIn = eng.inputPeakDb(ch); dOut = dIn + eng.gainReductionDb(ch) + (cur() ? cur()->fx.makeupDb : 0.0f); }
        if (on && !(kind == wm::engine::kMidSide)) { inTxt = juce::String(dIn, 1); outTxt = juce::String(dOut, 1); }
        meterIn.setDb(dIn); meterOut.setDb(dOut); meterIn.setAlpha(on ? 1.0f : 0.3f); meterOut.setAlpha(on ? 1.0f : 0.3f); repaint();
        for (int i = 0; i < chips.size(); ++i) { bool o = isOn(ids[size_t(i)]); chips[i]->setColour(juce::TextButton::buttonColourId, o ? col::teal.darker(0.4f) : col::panel); }
        onBtn.setButtonText(on ? "On" : "Off"); for (int i = 0; i < 4; ++i) knobs[i].setAlpha(on ? 1.0f : 0.45f);
    }
    wm::mixer::Mixer& mx; wm::engine::AudioEngine& eng; int ch = 0, kind = kEq; std::vector<int> ids;
    juce::ComboBox typeBox; juce::TextButton onBtn, close; Meter meterIn, meterOut; juce::Slider knobs[4]; juce::Label labels[4];
    juce::OwnedArray<juce::TextButton> chips; juce::String inTxt = "off", outTxt = "off";
};
