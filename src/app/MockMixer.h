// The Mixer page, ported natively from the approved mockup (no web view). Same layout, numbers measured in the mockup:
// three rows (hardware inputs, software playback, hardware outputs) plus the control room, 74 px strips with pan knob,
// M/S, peak readouts, mockup fader scale, T / wrench / FX / D buttons, gain box, ST and routing arrow, the 108 px channel
// settings column, the FX panel and the right sidebar. Every control writes into the mixer state the engine reads.
#pragma once
#include "MixerView.h"
#include "MockKit.h"

namespace wmui {
namespace mx_ {
const juce::Colour secBg(0xff1a1c1f), side(0xff22252a), bar(0xff1e2024), pill(0xff3a3e45), pillEdge(0xff555a63), slot(0xff1f2125),
    ink(0xffeceae4), mute(0xffb9b6ae), yel(0xffffd100), lav(0xffc0a9fe), knobEdge(0xff1a1c1f);
inline juce::Font f(float h, bool bold = false) { return juce::Font(h, bold ? juce::Font::bold : juce::Font::plain); }
inline juce::Font mono(float h) { return juce::Font(juce::Font::getDefaultMonospacedFontName(), h, juce::Font::plain); }
// Fader thumb top (px in the 120 px block) for a dB value, measured in the mockup.
inline float thumbTop(float db) {
    static const float d[7] = {6, 0, -6, -12, -24, -48, -60}, y[7] = {0, 12, 31, 48, 74, 105, 109};
    if (db >= d[0]) return y[0];
    for (int i = 0; i < 6; ++i) if (db >= d[i + 1]) return y[i] + (y[i + 1] - y[i]) * (d[i] - db) / (d[i] - d[i + 1]);
    return y[6];
}
inline float dbFromTop(float t) {
    static const float d[7] = {6, 0, -6, -12, -24, -48, -60}, y[7] = {0, 12, 31, 48, 74, 105, 109};
    if (t <= y[0]) return d[0];
    for (int i = 0; i < 6; ++i) if (t <= y[i + 1]) return d[i] + (d[i + 1] - d[i]) * (t - y[i]) / (y[i + 1] - y[i]);
    return -60.0f;
}
inline juce::String dbText(float db, int dp = 1) { return db <= -59.9f ? juce::String(juce::CharPointer_UTF8("-\xE2\x88\x9E")) : juce::String(db, dp); }
}  // namespace mx_

// Round knob drawn like the mockup's (dark disc, yellow indicator).
class MockKnob : public juce::Slider {
public:
    juce::Colour fill{0xff3a3e45};
    MockKnob() { setSliderStyle(RotaryVerticalDrag); setTextBoxStyle(NoTextBox, false, 0, 0); setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true); }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f); float d = juce::jmin(r.getWidth(), r.getHeight()); auto c = r.getCentre();
        g.setColour(fill); g.fillEllipse(c.x - d / 2, c.y - d / 2, d, d);
        g.setColour(mx_::knobEdge); g.drawEllipse(c.x - d / 2, c.y - d / 2, d, d, 2.0f);
        float ang = getRotaryParameters().startAngleRadians + float(valueToProportionOfLength(getValue())) * (getRotaryParameters().endAngleRadians - getRotaryParameters().startAngleRadians);
        g.setColour(mx_::yel); juce::Path p; p.addRectangle(-1.0f, -d / 2 + 1.0f, 2.0f, 8.0f);
        g.fillPath(p, juce::AffineTransform::rotation(ang).translated(c.x, c.y));
    }
};

// Fader block (67 x 120): dB scale, two meters, slot and the striped thumb, all at the mockup's pixel positions.
class MockFader : public juce::Component {
public:
    std::function<void(float)> onChange; float db = 0.0f, mL = -60.0f, mR = -60.0f; bool dragging = false;
    void set(float v) { db = juce::jlimit(-60.0f, 6.0f, v); repaint(); }
    void meters(float l, float r) { l = juce::jlimit(-60.0f, 6.0f, l); r = juce::jlimit(-60.0f, 6.0f, r); if (l != mL || r != mR) { mL = l; mR = r; repaint(); } }
    void paint(juce::Graphics& g) override {
        static const struct { const char* t; float db; int y; } sc[6] = {{"0", 0, -5}, {"-6", -6, 14}, {"-12", -12, 31}, {"-24", -24, 57}, {"-48", -48, 88}, {"\xE2\x88\x9E", -60, 115}};
        // the first label is "+6" in the mockup scale: tick text for the 0 dB mark shown as "0"
        g.setFont(mx_::f(7.0f)); g.setColour(mx_::mute);
        for (auto& s : sc) g.drawText(juce::CharPointer_UTF8(s.t), 0, s.y, 19, 9, juce::Justification::centredRight, false);
        for (int k = 0; k < 2; ++k) {
            auto box = juce::Rectangle<float>(21.0f + 6.0f * float(k), 11.0f, 5.0f, 109.0f);
            g.setColour(mx_::slot); g.fillRect(box);
            float lv = k == 0 ? mL : mR; float top = mx_::thumbTop(lv) + 11.0f;
            if (lv > -59.9f) {
                juce::ColourGradient gr(juce::Colour(0xffff1383), 0, box.getY(), juce::Colour(0xff6cc9f5), 0, box.getBottom(), false);
                gr.addColour(0.10, juce::Colour(0xffff1383)); gr.addColour(0.10001, juce::Colour(0xffa43dff)); gr.addColour(0.40, juce::Colour(0xffa43dff)); gr.addColour(0.40001, juce::Colour(0xff6cc9f5));
                g.setGradientFill(gr); g.fillRect(box.withTop(juce::jmax(box.getY(), top)));
            }
        }
        g.setColour(mx_::slot); g.fillRoundedRectangle(41.0f, 11.0f, 3.0f, 109.0f, 2.0f);
        auto th = juce::Rectangle<float>(34.5f, mx_::thumbTop(db), 16.0f, 22.0f);
        for (int y = 0; y < 22; y += 2) { g.setColour(((y / 2) % 2) == 0 ? juce::Colour(0xffcfcdc7) : juce::Colour(0xff8e8c87)); g.fillRect(th.getX(), th.getY() + float(y), 16.0f, 2.0f); }
        g.setColour(juce::Colour(0xff1a1c1f)); g.drawRoundedRectangle(th, 2.0f, 1.0f);
    }
    void mouseDown(const juce::MouseEvent& e) override { dragging = true; drag(e); }
    void mouseDrag(const juce::MouseEvent& e) override { drag(e); }
    void mouseUp(const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick(const juce::MouseEvent&) override { if (wm::mixer::settings().doubleClickReset) { set(0.0f); if (onChange) onChange(0.0f); } }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { set(db + w.deltaY * 3.0f); if (onChange) onChange(db); }
private:
    void drag(const juce::MouseEvent& e) { set(std::round(mx_::dbFromTop(float(e.y) - 11.0f) * 10.0f) / 10.0f); if (onChange) onChange(db); }
};

// One strip of the page. A channel strip (isBus false) edits mixer.channels[idx]; a bus strip edits mixer.buses[idx].
class MockStrip : public juce::Component {
public:
    static constexpr int kW = 74;
    std::function<void(MockStrip*)> onSelect, onWrench, onFx, onSplit, onRoute, onLook;
    bool isBus, selected = false; int idx;
    MockStrip(wm::mixer::Mixer& m, bool asBus, int i) : mx(m), isBus(asBus), idx(i) {
        auto tog = [this](MockButton& b, const juce::String& t, const char* tip) { b.setButtonText(t); b.setTooltip(tip); b.fill = mx_::pill; b.edge = mx_::pillEdge; b.textCol = mx_::mute; b.bold = true; b.fontSize = 8.0f; b.radius = 3.0f; addAndMakeVisible(b); };
        tog(m_, "M", "Mute"); tog(s_, "S", "Solo"); tog(dim_, "DIM", "Lower this output by 20 dB");
        for (auto* b : {&m_, &s_}) { b->fontSize = 9.0f; b->radius = 9.0f; }
        m_.fillOn = juce::Colour(0xffff1383); m_.textOn = juce::Colours::white; s_.fillOn = mx_::yel; s_.textOn = mx_::mute.darker(5.0f); dim_.fillOn = juce::Colour(0xffff9000); dim_.textOn = juce::Colours::black;
        tog(t_, "T", "Trim: the fader moves all routings of this channel (stored; the Digiface submix mapping is not decoded yet)"); t_.circle = true; t_.fillOn = juce::Colour(0xff01fcdc); t_.textOn = juce::Colours::black;
        tog(w_, juce::CharPointer_UTF8("\xF0\x9F\x94\xA7"), "Channel settings (stereo, width, M/S, phase, EQ, dynamics, send)"); tog(fx_, "FX", "Open this channel's FX panel"); tog(d_, "D", "Dynamics on/off (Off = bypassed, the sound passes untouched)");
        for (auto* b : {&w_, &fx_, &d_}) { b->fillOn = juce::Colour(0xffa43dff); b->textOn = juce::Colours::white; }
        d_.fillOn = juce::Colour(0xffa43dff); fx_.fillOn = juce::Colour(0xff01fcdc); fx_.textOn = juce::Colours::black; w_.fillOn = juce::Colour(0xffff9000); w_.textOn = juce::Colours::black;
        tog(st_, "ST", "Stereo: split a stereo strip into two mono strips, or join this mono strip with the next one"); st_.radius = 3.0f; st_.fillOn = juce::Colour(0xffff9000); st_.textOn = juce::Colours::black;
        tog(go_, juce::CharPointer_UTF8("\xE2\x96\xB8"), "Routing: where this strip goes"); go_.radius = 3.0f;
        pan.setRange(-1.0, 1.0, 0.01); pan.setTooltip("Pan (pair: balance); double-click resets"); addAndMakeVisible(pan);
        addAndMakeVisible(fader); fader.toBack(); addAndMakeVisible(gainBox);
        gainBox.setJustificationType(juce::Justification::centred); gainBox.setEditable(false, true); gainBox.setFont(mx_::mono(9.0f)); gainBox.setColour(juce::Label::textColourId, mx_::yel);
        gainBox.setTooltip("Fader value in dB; double-click to type one"); gainBox.setColour(juce::Label::backgroundColourId, mx_::slot);
        gainBox.onTextChange = [this] { float v = gainBox.getText().trim().startsWith("-") && gainBox.getText().contains("inf") ? -60.0f : gainBox.getText().getFloatValue(); setGain(juce::jlimit(-60.0f, 6.0f, v)); refresh(); };
        fader.onChange = [this](float v) { setGain(v); gainBox.setText(mx_::dbText(v), juce::dontSendNotification); };
        pan.onValueChange = [this] { if (!isBus) ch().pan = float(pan.getValue()); };
        m_.onClick = [this] { if (!isBus) { ch().mute = !ch().mute; refresh(); } };
        s_.onClick = [this] { if (!isBus) { ch().solo = !ch().solo; refresh(); } };
        dim_.onClick = [this] { if (isBus) { bus().dim = !bus().dim; refresh(); } };
        t_.onClick = [this] { if (!isBus) { ch().trim = !ch().trim; refresh(); } };
        d_.onClick = [this] { if (!isBus) { ch().fx.compOn = !ch().fx.compOn; refresh(); } };
        w_.onClick = [this] { if (onWrench) onWrench(this); }; fx_.onClick = [this] { if (onFx) onFx(this); };
        st_.onClick = [this] { if (onSplit) onSplit(this); }; go_.onClick = [this] { if (onRoute) onRoute(this); };
        refresh();
    }
    wm::mixer::Channel& ch() { return mx.state().channels[size_t(idx)]; }
    wm::mixer::Bus& bus() { return mx.state().buses[size_t(idx)]; }
    juce::String name() { return isBus ? juce::String(bus().name) : juce::String(ch().name); }
    juce::Colour colour() { return isBus ? juce::Colour(0xff6a6f7a) : stripColour(ch().colour); }
    void setLevels(float l, float r) { fader.meters(l, r); if (l > pkL) pkL = l; if (r > pkR) pkR = r; }
    void setLit(bool fxOpen, bool setOpen) { fx_.setToggleState(fxOpen, juce::dontSendNotification); w_.setToggleState(setOpen, juce::dontSendNotification); }
    void showParts(bool fxB, bool trimB, bool names) { fx_.setVisible(!isBus && fxB); t_.setVisible(!isBus && trimB); showNames = names; repaint(); }
    void refresh() {
        float g = isBus ? bus().volumeDb : ch().gainDb;
        fader.set(g); gainBox.setText(mx_::dbText(g), juce::dontSendNotification);
        if (isBus) { dim_.setToggleState(bus().dim, juce::dontSendNotification); }
        else {
            auto& c = ch(); m_.setToggleState(c.mute, juce::dontSendNotification); s_.setToggleState(c.solo, juce::dontSendNotification); t_.setToggleState(c.trim, juce::dontSendNotification);
            d_.setToggleState(c.fx.compOn, juce::dontSendNotification); st_.setToggleState(c.stereoLinked, juce::dontSendNotification); pan.setValue(c.pan, juce::dontSendNotification);
            pan.setDoubleClickReturnValue(wm::mixer::settings().doubleClickReset, 0.0);
        }
        resized(); repaint();
    }
    void paint(juce::Graphics& g) override {
        auto b = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff25282d)); g.fillRect(b.withTrimmedRight(1.0f));
        g.setColour(juce::Colour(0xff1a1c1f)); g.fillRect(b.getRight() - 1.0f, 0.0f, 1.0f, b.getHeight());
        g.setColour(colour()); g.fillRect(0.0f, 0.0f, float(kW - 1), 3.0f);
        g.setColour(mx_::bar); g.fillRect(0.0f, 3.0f, float(kW - 1), 18.0f);
        std::string em = isBus ? std::string() : ch().emoji; juce::String nm = name();
        g.setColour(mx_::ink); g.setFont(mx_::f(10.0f, true));
        g.drawText((em.empty() ? juce::String() : juce::String(juce::CharPointer_UTF8(em.c_str())) + " ") + nm, 2, 3, kW - 5, 18, juce::Justification::centred, true);
        if (selected) { g.setColour(juce::Colour(0xff01fcdc)); g.drawRect(getLocalBounds().withTrimmedRight(1), 1); }
        if (!isBus) {   // M/S pill, pan value box
            g.setColour(mx_::pill); g.fillRoundedRectangle(11.0f, 55.0f, 52.0f, 18.0f, 10.0f);
            g.setColour(mx_::slot); g.fillRoundedRectangle(44.0f, 32.0f, 14.0f, 13.0f, 3.0f);
            g.setColour(mx_::yel); g.setFont(mx_::mono(10.0f));
            float p = ch().pan; g.drawText(juce::String(int(std::round(std::fabs(p) * 50))), 44, 32, 14, 13, juce::Justification::centred, false);
        } else { g.setColour(mx_::pill); g.fillRoundedRectangle(11.0f, 55.0f, 52.0f, 18.0f, 10.0f); }
        g.setFont(mx_::f(8.0f)); g.setColour(mx_::mute);
        g.drawText(mx_::dbText(pkL, 0) + " " + mx_::dbText(pkR, 0), 14, 75, 46, 11, juce::Justification::centred, false);
        if (showNames) { g.setFont(mx_::f(9.0f)); g.setColour(mx_::mute); g.drawText(hwLabel(), 0, 230, kW - 1, 12, juce::Justification::centred, false); }
    }
    void resized() override {
        pan.setBounds(14, 25, 26, 26); pan.setVisible(!isBus);
        m_.setBounds(12, 56, 24, 16); s_.setBounds(38, 56, 24, 16); m_.setVisible(!isBus); s_.setVisible(!isBus);
        dim_.setBounds(14, 56, 46, 16); dim_.setVisible(isBus);
        fader.setBounds(3, 90, 67, 120);
        t_.setBounds(55, 92, 18, 18);
        int y = 90 + 120 - 3 * 18 - 2 * 3 - 0; w_.setBounds(52, y + 0, 18, 18); fx_.setBounds(52, y + 21, 18, 18); d_.setBounds(52, y + 42, 18, 18);
        d_.setVisible(!isBus); w_.setVisible(true); if (isBus) { w_.setVisible(false); fx_.setVisible(false); t_.setVisible(false); }
        gainBox.setBounds(6, 214, 20, 14); st_.setBounds(28, 214, 22, 14); go_.setBounds(53, 214, 14, 14); st_.setVisible(!isBus);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (onSelect) onSelect(this);
        if (e.mods.isPopupMenu() && !isBus) lookMenu();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (e.y < 21) rename(); }
    void rename() {
        auto* aw = new juce::AlertWindow("Rename", "New name:", juce::MessageBoxIconType::NoIcon);
        aw->addTextEditor("n", name()); aw->addButton("OK", 1); aw->addButton("Cancel", 0);
        aw->enterModalState(true, juce::ModalCallbackFunction::create([this, aw, safe = juce::Component::SafePointer<MockStrip>(this)](int r) {
            if (r == 1 && safe) { auto n = aw->getTextEditorContents("n").toStdString(); if (!n.empty()) { if (isBus) bus().name = n; else mx.rename(idx, n); repaint(); if (onLook) onLook(this); } }
            delete aw; }), false);
    }
private:
    void lookMenu() {
        juce::PopupMenu m, cm, em; m.addItem(1, "Rename...");
        for (int i = 0; i < 9; ++i) cm.addItem(100 + i, stripColourName(i), true, ch().colour == i);
        for (size_t i = 0; i < emojiList().size(); ++i) em.addItem(200 + int(i), emojiList()[i].empty() ? juce::String("(no icon)") : juce::String(juce::CharPointer_UTF8(emojiList()[i].c_str())), true, emojiIndex(ch().emoji) == int(i));
        m.addSubMenu("Colour", cm); m.addSubMenu("Icon", em);
        m.showMenuAsync(juce::PopupMenu::Options(), [this, safe = juce::Component::SafePointer<MockStrip>(this)](int r) {
            if (!safe || r == 0) return;
            if (r == 1) rename(); else if (r >= 200) ch().emoji = emojiList()[size_t(r - 200)]; else ch().colour = r - 100;
            repaint(); if (onLook) onLook(this); });
    }
    juce::String hwLabel() {
        int h = isBus ? bus().hwFirst : ch().hwFirst; bool st = isBus ? bus().stereo : ch().stereoLinked;
        if (h < 0) return isBus ? "software" : "";
        if (h >= 32) return "PH";
        return "ADAT " + juce::String(h + 1) + (st ? "/" + juce::String(h + 2) : juce::String());
    }
    void setGain(float v) { if (isBus) bus().volumeDb = v; else ch().gainDb = v; }
    wm::mixer::Mixer& mx; bool showNames = true; float pkL = -60.0f, pkR = -60.0f;
    MockKnob pan; MockFader fader; juce::Label gainBox; MockButton m_, s_, dim_, t_, w_, fx_, d_, st_, go_;
public:
    void clearPeaks() { pkL = pkR = -60.0f; repaint(); }
};

// The 108 px settings column of the selected channel.
class MockSettings : public juce::Component {
public:
    std::function<void()> onClose, onChanged, onSplit;
    MockSettings(wm::mixer::Mixer& m) : mx(m) {
        auto tog = [this](MockButton& b, const juce::String& t, const char* tip, juce::Colour on) { b.setButtonText(t); b.setTooltip(tip); b.fill = mx_::pill; b.edge = mx_::pillEdge; b.textCol = mx_::mute; b.bold = true; b.fontSize = 9.0f; b.radius = 4.0f; b.fillOn = on; b.textOn = juce::Colours::black; addAndMakeVisible(b); };
        tog(stereo, "Stereo", "Stereo strip on/off: splits a pair into two mono strips, or joins with the next strip", juce::Colour(0xffff9000)); stereo.radius = 11.0f;
        tog(ms, "M/S Proc", "Input pair is Mid/Side: decode to left/right (stereo strip only)", juce::Colour(0xffff9000));
        tog(pl, juce::CharPointer_UTF8("\xC3\x98L"), "Invert phase of the left channel", juce::Colour(0xffff9000)); tog(pr, juce::CharPointer_UTF8("\xC3\x98R"), "Invert phase of the right channel (stereo strip)", juce::Colour(0xffff9000));
        tog(fxB, "FX", "EQ on/off for this channel (Off = bypassed)", juce::Colour(0xff01fcdc)); tog(dynB, "Dynamics", "Dynamics on/off for this channel (Off = bypassed)", juce::Colour(0xffa43dff));
        close.setButtonText(juce::CharPointer_UTF8("\xC3\x97")); close.setTooltip("Close this panel"); close.fill = juce::Colours::transparentBlack; close.edge = juce::Colours::transparentBlack; close.fontSize = 13.0f; addAndMakeVisible(close);
        for (auto* k : {&width, &send}) { k->setTooltip(k == &width ? "Stereo width: 0 = mono, 1 = unchanged, 2 = double (stereo strip)" : "Send to the FX bus (last bus). Fully left = off"); addAndMakeVisible(*k); }
        width.setRange(0.0, 2.0, 0.01); width.setDoubleClickReturnValue(true, 1.0); send.setRange(-60.0, 0.0, 0.5); send.setDoubleClickReturnValue(true, -60.0);
        close.onClick = [this] { if (onClose) onClose(); };
        auto ch_ = [this] { onChanged ? onChanged() : void(); };
        stereo.onClick = [this] { if (onSplit) onSplit(); };
        ms.onClick = [this, ch_] { c().midSide = !c().midSide; sync(); ch_(); }; pl.onClick = [this, ch_] { c().phaseL = !c().phaseL; sync(); ch_(); }; pr.onClick = [this, ch_] { c().phaseR = !c().phaseR; sync(); ch_(); };
        fxB.onClick = [this, ch_] { c().fx.eqOn = !c().fx.eqOn; sync(); ch_(); }; dynB.onClick = [this, ch_] { c().fx.compOn = !c().fx.compOn; sync(); ch_(); };
        width.onValueChange = [this] { c().width = float(width.getValue()); }; send.onValueChange = [this] { c().fxSendDb = float(send.getValue()); };
    }
    void setChannel(int i) { ch = i; sync(); }
    void sync() {
        if (ch < 0 || ch >= int(mx.state().channels.size())) return; auto& s = c();
        stereo.setToggleState(s.stereoLinked, juce::dontSendNotification); ms.setToggleState(s.midSide, juce::dontSendNotification);
        pl.setToggleState(s.phaseL, juce::dontSendNotification); pr.setToggleState(s.phaseR, juce::dontSendNotification);
        fxB.setToggleState(s.fx.eqOn, juce::dontSendNotification); dynB.setToggleState(s.fx.compOn, juce::dontSendNotification);
        width.setValue(s.width, juce::dontSendNotification); send.setValue(s.fxSendDb, juce::dontSendNotification);
        width.setEnabled(s.stereoLinked); ms.setEnabled(s.stereoLinked); pr.setEnabled(s.stereoLinked); repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(juce::Colour(0xff33363c)); g.fillRect(getLocalBounds());
        g.setColour(juce::Colour(0xffff1383)); g.setFont(mx_::f(11.0f, true)); g.drawText("Settings", 6, 4, 80, 16, juce::Justification::centredLeft, false);
        g.setColour(mx_::mute); g.setFont(mx_::f(8.0f)); g.drawText("Width", wRect, juce::Justification::centred, false); g.drawText("FX SEND", sRect, juce::Justification::centred, false);
        g.setColour(mx_::yel); g.setFont(mx_::mono(9.0f)); g.drawText(juce::String(width.getValue(), 2), 72, 58, 32, 12, juce::Justification::centred, false);
        g.drawText(send.getValue() <= -59.9 ? juce::String("off") : juce::String(send.getValue(), 1), 72, 196, 32, 12, juce::Justification::centred, false);
    }
    void resized() override {
        close.setBounds(getWidth() - 24, 2, 20, 18); stereo.setBounds(6, 22, 96, 22);
        width.setBounds(38, 48, 32, 32); wRect = juce::Rectangle<int>(6, 82, 96, 10); ms.setBounds(6, 96, 96, 18);
        pl.setBounds(6, 118, 46, 18); pr.setBounds(56, 118, 46, 18); fxB.setBounds(6, 140, 96, 18); dynB.setBounds(6, 162, 96, 18);
        send.setBounds(38, 186, 32, 32); sRect = juce::Rectangle<int>(6, 220, 96, 10);
        wRect = wRect.withY(82); }
private:
    wm::mixer::Channel& c() { return mx.state().channels[size_t(ch)]; }
    wm::mixer::Mixer& mx; int ch = 0; juce::Rectangle<int> wRect, sRect;
    MockButton stereo, ms, pl, pr, fxB, dynB, close; MockKnob width, send;
};

// FX panel beside the strip: 214 x 366 (see FxChainView.h for the logic; this is its mockup layout).
using MockFxPanel = FxChainPanel;

// One row of strips with its header bar (collapse arrow, title, hint, scrollbar, page counter).
class MockRow : public juce::Component {
public:
    static constexpr int kBar = 16, kStripH = 247;
    std::function<void()> onCollapse;
    MockRow(const juce::String& title, const juce::String& hint, bool purple = false) : titleTxt(title), hintTxt(hint), border(purple ? juce::Colour(0xffa43dff) : juce::Colour(0xff4a4e57)) { addAndMakeVisible(inner); inner.setInterceptsMouseClicks(false, true); }
    juce::Component inner; bool collapsed = false; int contentW = 0, offset = 0, innerH = kStripH;
    std::function<void(float)> onWheelY;
    int fullH() const { return collapsed ? kBar + 2 : innerH + kBar + 2; }
    void setContentWidth(int w) { contentW = w; offset = juce::jlimit(0, juce::jmax(0, contentW - viewW()), offset); layoutInner(); repaint(); }
    int viewW() const { return getWidth() - 2; }
    int pages() const { return juce::jmax(1, int(std::ceil(double(contentW) / juce::jmax(1, viewW())))); }
    int page() const { return juce::jmin(pages() - 1, offset / juce::jmax(1, viewW())); }
    void scrollTo(int x) { offset = juce::jlimit(0, juce::jmax(0, contentW - viewW()), x); layoutInner(); repaint(); }
    int headH() const { return kBar; }
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(mx_::secBg); g.fillRoundedRectangle(r, 6.0f); g.setColour(border); g.drawRoundedRectangle(r, 6.0f, 1.0f);
        auto bar = barRect(); g.setColour(mx_::bar); g.fillRect(bar);
        g.setColour(mx_::mute); g.setFont(mx_::f(9.0f, true)); g.drawText(collapsed ? juce::CharPointer_UTF8("\xE2\x96\xB8") : juce::CharPointer_UTF8("\xE2\x96\xBE"), bar.getX() + 4, bar.getY(), 12, kBar, juce::Justification::centred, false);
        g.drawText(titleTxt, bar.getX() + 18, bar.getY(), 110, kBar, juce::Justification::centredLeft, false);
        g.setColour(mx_::mute.withAlpha(0.6f)); g.setFont(mx_::f(8.0f)); if (getWidth() > 400) g.drawText(hintTxt, bar.getX() + 128, bar.getY(), 190, kBar, juce::Justification::centredLeft, false);
        if (!collapsed && contentW > viewW()) {
            auto tr = trackRect(); g.setColour(mx_::slot); g.fillRoundedRectangle(tr, 2.0f);
            g.setColour(juce::Colour(0xff6a6f7a)); g.fillRoundedRectangle(thumbRect(), 2.0f);
        }
        if (!collapsed && pages() > 1) {
            g.setColour(mx_::mute); g.setFont(mx_::f(9.0f)); int x = bar.getRight() - 12 - 12 * pages() - 14;
            g.drawText(juce::CharPointer_UTF8("\xE2\x97\x82"), x, bar.getY(), 12, kBar, juce::Justification::centred, false);
            for (int p = 0; p < pages(); ++p) { g.setColour(p == page() ? juce::Colour(0xff01fcdc) : mx_::mute); g.drawText(juce::String(p + 1), x + 12 + 12 * p, bar.getY(), 12, kBar, juce::Justification::centred, false); }
            g.setColour(mx_::mute); g.drawText(juce::CharPointer_UTF8("\xE2\x96\xB8"), x + 12 + 12 * pages(), bar.getY(), 12, kBar, juce::Justification::centred, false);
        }
    }
    void resized() override { layoutInner(); }
    void layoutInner() { inner.setBounds(1 - offset, 1, juce::jmax(contentW, viewW()), innerH); inner.setVisible(!collapsed); }
    void mouseDown(const juce::MouseEvent& e) override {
        auto bar = barRect().toNearestInt();
        if (e.y >= bar.getY() && e.x < bar.getX() + 18) { collapsed = !collapsed; if (onCollapse) onCollapse(); return; }
        if (contentW > viewW() && e.y >= bar.getY()) {
            auto t = thumbRect(); if (t.expanded(0, 6).contains(e.position)) { dragX = e.x; dragOff = offset; return; }
            if (trackRect().expanded(0, 6).contains(e.position)) { scrollTo(int((e.x - trackRect().getX()) / trackRect().getWidth() * float(contentW)) - viewW() / 2); return; }
            int x = bar.getRight() - 12 - 12 * pages() - 14;
            if (e.x >= x && e.x < x + 12) scrollTo(offset - viewW()); else if (e.x >= x + 12 + 12 * pages()) scrollTo(offset + viewW());
            else if (e.x >= x + 12 && e.x < x + 12 + 12 * pages()) scrollTo((e.x - x - 12) / 12 * viewW());
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override { if (dragX >= 0) scrollTo(dragOff + int(float(e.x - dragX) / trackRect().getWidth() * float(contentW))); }
    void mouseUp(const juce::MouseEvent&) override { dragX = -1; }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (std::fabs(w.deltaX) > std::fabs(w.deltaY) || e.mods.isShiftDown()) scrollTo(offset - int((std::fabs(w.deltaX) > std::fabs(w.deltaY) ? w.deltaX : w.deltaY) * 240.0f));
        else if (onWheelY) onWheelY(w.deltaY); }
    juce::Rectangle<float> barRect() const { return juce::Rectangle<float>(1.0f, collapsed ? 0.5f : float(innerH + 1), float(getWidth() - 2), float(kBar)); }
private:
    juce::Rectangle<float> trackRect() const { auto b = barRect(); return juce::Rectangle<float>(b.getX() + 266.0f, b.getY() + 5.5f, juce::jmax(40.0f, b.getWidth() - 266.0f - 120.0f), 5.0f); }
    juce::Rectangle<float> thumbRect() const { auto t = trackRect(); float f = float(viewW()) / float(juce::jmax(1, contentW)); float w = juce::jmax(16.0f, t.getWidth() * f); float x = t.getX() + (t.getWidth() - w) * (contentW > viewW() ? float(offset) / float(contentW - viewW()) : 0.0f); return {x, t.getY(), w, t.getHeight()}; }
    juce::String titleTxt, hintTxt; juce::Colour border; int dragX = -1, dragOff = 0;
};

class MockMixerView : public juce::Component, private juce::Timer {
public:
    int selectedChannel = 0;
    std::function<void(int)> onSelect;
    std::function<void()> onOpenFx, onUndo, onRedo, onOpenRouting, onOpenReverb, onStructure;
    std::function<std::string()> deviceStatus;
    MockMixerView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, wm::engine::AudioEngine& e)
        : mx(m), rt(r), eng(e), rIn("HARDWARE INPUTS", "window grows as channels are added"), rPb("SOFTWARE PLAYBACK", "playback from the computer"), rOut("HARDWARE OUTPUTS", "outputs of the interface"), rCr("CONTROL ROOM", "", true),
          settings(m), fxPanel(m, e) {
        addAndMakeVisible(body);
        for (auto* row : {&rIn, &rPb, &rOut, &rCr}) { body.addAndMakeVisible(*row); row->onCollapse = [this] { relayoutRows(); }; row->onWheelY = [this](float dy) { scrollBy(dy); }; }
        addChildComponent(settings); addChildComponent(fxPanel);
        settings.onClose = [this] { setPanelOpen(false); }; settings.onChanged = [this] { refreshStrips(); }; settings.onSplit = [this] { split(selectedChannel); };
        fxPanel.onClose = [this] { setFxOpen(false); };
        setupSidebar(); rebuild(); pick(0); startTimerHz(30);
    }
    bool fxOpen() const { return fxOpen_; } bool panelOpen() const { return panelOpen_; }
    void setFxOpen(bool o) { fxOpen_ = o; fxPanel.setVisible(o); if (o) fxPanel.setChannel(selectedChannel); updateLit(); relayoutRows(); }
    void setPanelOpen(bool o) { panelOpen_ = o; settings.setVisible(o); updateLit(); relayoutRows(); }
    void select(int c) { pick(c); }
    void pick(int c) {
        selectedChannel = juce::jlimit(0, juce::jmax(0, int(mx.state().channels.size()) - 1), c);
        for (auto& s : strips) { s->selected = !s->isBus && s->idx == selectedChannel; s->repaint(); }
        settings.setChannel(selectedChannel); if (fxOpen_) fxPanel.setChannel(selectedChannel);
        updateLit(); relayoutRows(); if (onSelect) onSelect(selectedChannel);
    }
    void refreshAll() { rebuild(); for (auto& s : strips) s->refresh(); refreshSnaps(); settings.sync(); }
    void paint(juce::Graphics&) override {}
    void resized() override {
        auto r = getLocalBounds(); int sideW = 150; auto sb = r.removeFromRight(sideW); r.removeFromRight(8);
        sidebar.setBounds(sb.withHeight(511)); layoutSidebar();
        areaW = r.getWidth(); body.setSize(areaW, getHeight()); relayoutRows();
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scrollBy(w.deltaY); }
private:
    void timerCallback() override {
        for (auto& s : strips) {
            if (!s->isShowing()) continue;
            if (s->isBus) { int b = s->idx; s->setLevels(eng.outputPeakDb(b * 2), eng.outputPeakDb(b * 2 + 1)); }
            else { float p = eng.inputPeakDb(s->idx); s->setLevels(p, p); }
        }
        if (deviceStatus && devLabel.getText() != juce::String(deviceStatus())) devLabel.setText(deviceStatus(), juce::dontSendNotification);
    }
    // ---- structure
    void rebuild() {
        strips.clear(); stripsOwned.clear();
        auto add = [this](MockRow& row, bool bus, int i) {
            auto* s = new MockStrip(mx, bus, i); strips.emplace_back(s); row.inner.addAndMakeVisible(*s);
            s->onSelect = [this](MockStrip* st) { if (!st->isBus) pick(st->idx); };
            s->onWrench = [this](MockStrip* st) { bool same = st->idx == selectedChannel; pick(st->idx); setPanelOpen(!(same && panelOpen_)); };
            s->onFx = [this](MockStrip* st) { bool same = st->idx == selectedChannel; pick(st->idx); setFxOpen(!(same && fxOpen_)); };
            s->onSplit = [this](MockStrip* st) { split(st->idx); };
            s->onRoute = [this](MockStrip*) { if (onOpenRouting) onOpenRouting(); };
            s->onLook = [this](MockStrip*) { refreshStrips(); };
            s->showParts(showFx, showTrim, showNames);
        };
        for (int i = 0; i < int(mx.state().channels.size()); ++i) { auto& c = mx.state().channels[size_t(i)]; if (c.hidden) continue; add(c.playback ? rPb : rIn, false, i); }
        for (int b = int(mx.state().buses.size()) - 1; b >= 0; --b) if (b < 2) add(rCr, true, b);   // control room shows the speakers right to left like the mockup
        for (int b = 2; b < int(mx.state().buses.size()); ++b) add(rOut, true, b);
        selectedChannel = juce::jlimit(0, juce::jmax(0, int(mx.state().channels.size()) - 1), selectedChannel);
        for (auto& s : strips) s->selected = !s->isBus && s->idx == selectedChannel;
        updateLit(); relayoutRows();
    }
    void refreshStrips() { for (auto& s : strips) s->refresh(); settings.sync(); }
    void split(int c) {
        if (c < 0 || c >= int(mx.state().channels.size())) return;
        if (mx.state().channels[size_t(c)].stereoLinked) { if (mx.splitStereo(c)) rt.duplicateInput(c); }
        else if (mx.joinStereo(c)) rt.eraseInput(c + 1);
        else return;
        eng.sync(); if (onStructure) onStructure(); rebuild(); pick(c);
    }
    void updateLit() { for (auto& s : strips) if (!s->isBus) s->setLit(fxOpen_ && s->idx == selectedChannel, panelOpen_ && s->idx == selectedChannel); }
    MockStrip* stripFor(int c) { for (auto& s : strips) if (!s->isBus && s->idx == c) return s.get(); return nullptr; }
    // lay the strips out in each row; the settings column and the FX panel sit right after the selected strip of its row
    void relayoutRows() {
        MockRow* rows[4] = {&rIn, &rPb, &rOut, &rCr};
        for (auto* row : rows) {
            int x = 0; bool hostsFx = false;
            for (auto& s : strips) {
                if (s->getParentComponent() != &row->inner) continue;
                s->setBounds(x, 0, MockStrip::kW, 247); x += MockStrip::kW;
                if (!s->isBus && s->idx == selectedChannel) {
                    if (panelOpen_) { row->inner.addAndMakeVisible(settings); settings.setBounds(x, 0, 108, 247); x += 108; }
                    if (fxOpen_) { row->inner.addAndMakeVisible(fxPanel); fxPanel.setBounds(x, 0, 214, 366); x += 214; hostsFx = true; }
                }
            }
            row->innerH = hostsFx ? 372 : MockRow::kStripH;
            row->setContentWidth(x);
        }
        int w = areaW, y = 0;
        rIn.setBounds(0, y, w, rIn.fullH()); y += rIn.fullH() + 6;
        rPb.setBounds(0, y, w, rPb.fullH()); y += rPb.fullH() + 6;
        rOut.setBounds(0, y, w - 175, rOut.fullH()); rCr.setBounds(w - 169, y, 169, rCr.fullH()); y += juce::jmax(rOut.fullH(), rCr.fullH());
        for (auto* row : rows) row->setContentWidth(row->contentW);
        totalH = y; body.setBounds(0, -scrollY(), areaW, totalH); repaint();
    }
    int scrollPx = 0; int totalH = 0;
    int scrollY() { scrollPx = juce::jlimit(0, juce::jmax(0, totalH - getHeight()), scrollPx); return scrollPx; }
    void scrollBy(float dy) { scrollPx -= int(dy * 160.0f); body.setTopLeftPosition(0, -scrollY()); }
    // ---- sidebar
    void setupSidebar() {
        addAndMakeVisible(sidebar); sidebar.setInterceptsMouseClicks(false, true);
        auto b = [this](MockButton& x, const juce::String& t, const char* tip, std::function<void()> f) { x.setButtonText(t); x.setTooltip(tip); x.fill = mx_::pill; x.edge = mx_::pillEdge; x.textCol = mx_::ink; x.fontSize = 9.0f; x.bold = true; x.radius = 4.0f; x.onClick = std::move(f); sidebar.addAndMakeVisible(x); };
        devLabel.setFont(mx_::f(9.0f)); devLabel.setMinimumHorizontalScale(0.7f); devLabel.setColour(juce::Label::textColourId, mx_::ink); devLabel.setColour(juce::Label::backgroundColourId, mx_::slot); devLabel.setText("Digiface USB", juce::dontSendNotification);
        devLabel.setTooltip("Interface and connection status"); devLabel.setJustificationType(juce::Justification::centred); sidebar.addAndMakeVisible(devLabel);
        b(undoB, juce::CharPointer_UTF8("\xE2\x86\xB6"), "Undo", [this] { if (onUndo) onUndo(); }); b(redoB, juce::CharPointer_UTF8("\xE2\x86\xB7"), "Redo", [this] { if (onRedo) onRedo(); });
        for (auto* x : {&undoB, &redoB}) { x->circle = true; x->fontSize = 12.0f; }
        b(muteAll, "M", "Clear all mutes", [this] { for (auto& c : mx.state().channels) c.mute = false; refreshStrips(); });
        b(soloAll, "S", "Clear all solos", [this] { for (auto& c : mx.state().channels) c.solo = false; refreshStrips(); });
        for (auto* x : {&muteAll, &soloAll}) { x->circle = true; x->fontSize = 9.0f; }
        auto sh = [this, &b](MockButton& x, const juce::String& t, const char* tip, bool* flag) { b(x, t, tip, [this, &x, flag] { *flag = !*flag; x.setToggleState(*flag, juce::dontSendNotification); applyShow(); }); x.setToggleState(*flag, juce::dontSendNotification); x.fill = juce::Colour(0xff3308af); x.edge = juce::Colour(0xff3308af); x.radius = 8.0f; x.fontSize = 8.0f; x.fillOn = juce::Colour(0xff01fcdc); x.textOn = juce::Colours::black; };
        sh(showFxB, "FX", "Show the FX button on channel strips", &showFx); sh(showTrimB, "TRIM", "Show the T (trim) button on channel strips", &showTrim); sh(showNamesB, "NAMES", "Show the hardware label under each strip", &showNames);
        b(storeB, "Store", "Store the current mix as a snapshot", [this] { int n = int(mx.snapshotNames().size()) + 1; mx.store("Snapshot " + std::to_string(n)); refreshSnaps(); });
        b(reverbB, juce::CharPointer_UTF8("FX \xC2\xB7 Reverb / Echo   \xE2\x96\xB8"), "Reverb and echo of the FX bus", [this] { if (onOpenReverb) onOpenReverb(); });
        reverbB.fill = juce::Colour(0xff3308af); reverbB.edge = juce::Colour(0xff3308af); reverbB.textCol = juce::Colours::white; reverbB.radius = 3.0f; reverbB.fontSize = 9.0f;
        snaps.setTooltip("Snapshots: click to recall"); snaps.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack); snaps.setModel(&snapModel); snapModel.owner = this; snaps.setRowHeight(14); sidebar.addAndMakeVisible(snaps);
    }
    void applyShow() { for (auto& s : strips) s->showParts(showFx, showTrim, showNames); }
    void refreshSnaps() { snapNames = mx.snapshotNames(); snaps.updateContent(); snaps.repaint(); }
    void layoutSidebar() {
        devLabel.setBounds(6, 6, 138, 22); undoB.setBounds(42, 34, 30, 22); redoB.setBounds(78, 34, 30, 22);
        muteAll.setBounds(40, 62, 22, 22); soloAll.setBounds(88, 62, 22, 22);
        showFxB.setBounds(14, 126, 58, 16); showTrimB.setBounds(78, 126, 58, 16); showNamesB.setBounds(14, 146, 58, 16);
        snaps.setBounds(10, 207, 130, 150); storeB.setBounds(6, 362, 138, 20); reverbB.setBounds(6, 392, 138, 18);
    }
    struct Side : juce::Component {
        void paint(juce::Graphics& g) override {
            auto r = getLocalBounds().toFloat().reduced(0.5f); g.setColour(mx_::side); g.fillRoundedRectangle(r, 6.0f); g.setColour(juce::Colour(0xff4a4e57)); g.drawRoundedRectangle(r, 6.0f, 1.0f);
            auto bar = [&](int y, const char* t) { g.setColour(juce::Colour(0xff3308af)); g.fillRoundedRectangle(6.0f, float(y), 138.0f, 18.0f, 3.0f); g.setColour(juce::Colours::white); g.setFont(mx_::f(10.0f, true)); g.drawText(t, 12, y, 120, 18, juce::Justification::centredLeft, false); };
            bar(92, "View options"); g.setColour(juce::Colour(0xff774bf9)); g.fillRect(6, 110, 138, 58);
            g.setColour(juce::Colours::white); g.setFont(mx_::f(7.0f, true)); g.drawText("SHOW", 6, 114, 138, 10, juce::Justification::centred, false);
            bar(176, "Snapshots");
        }
    };
    struct SnapModel : juce::ListBoxModel {
        MockMixerView* owner = nullptr;
        int getNumRows() override { return owner ? int(owner->snapNames.size()) : 0; }
        void paintListBoxItem(int r, juce::Graphics& g, int w, int h, bool sel) override {
            if (!owner || r >= int(owner->snapNames.size())) return;
            g.setColour(sel ? juce::Colour(0xff774bf9) : juce::Colour(0xff3a3e45)); g.fillRect(2, 3, 8, 8);
            g.setColour(mx_::ink); g.setFont(mx_::f(9.0f)); g.drawText(owner->snapNames[size_t(r)], 16, 0, w - 18, h, juce::Justification::centredLeft, true);
        }
        void listBoxItemClicked(int r, const juce::MouseEvent&) override { if (owner && r < int(owner->snapNames.size())) { owner->mx.recall(owner->snapNames[size_t(r)]); owner->refreshAll(); } }
    };
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; wm::engine::AudioEngine& eng;
    MockRow rIn, rPb, rOut, rCr; MockSettings settings; MockFxPanel fxPanel;
    std::vector<std::unique_ptr<MockStrip>> strips, stripsOwned;
    Side sidebar; juce::Label devLabel; MockButton undoB, redoB, muteAll, soloAll, showFxB, showTrimB, showNamesB, storeB, reverbB; juce::Component body; juce::ListBox snaps; SnapModel snapModel; std::vector<std::string> snapNames;
    bool fxOpen_ = false, panelOpen_ = false, showFx = true, showTrim = true, showNames = true; int areaW = 816, settingsX = 0;
};
}  // namespace wmui
