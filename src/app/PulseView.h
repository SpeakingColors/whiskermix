// Pulse 16 page: connection, clock, headphones, levels, routing, presets and unit settings.
// All values come from wm::pulse16::Session. Nothing is sent until the user changes a control, and the page shows
// what the unit reported on connect (the unit's own screen can disagree with what it is really running).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <memory>
#include "PulseLink.h"
#include "MixerView.h"
#include "Look.h"

namespace wmui {

class PulseView : public juce::Component, private juce::Timer {
public:
    PulseView() : session(link) {
        link.onSysEx = [this](const wm::pulse16::Bytes& m) { feedSysEx(m); };
        auto lab = [this](juce::Label& l, const char* t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(12.5f); addAndMakeVisible(l); };
        auto btn = [this](juce::TextButton& b, const char* t, std::function<void()> f, const char* tip) { b.setButtonText(t); b.setTooltip(tip); b.onClick = std::move(f); addAndMakeVisible(b); };

        lab(inLab, "MIDI in"); lab(outLab, "MIDI out");
        addAndMakeVisible(inBox); addAndMakeVisible(outBox);
        btn(findB, "Find Pulse", [this] { findPulse(); }, "Tries each MIDI port pair and keeps the one the Pulse answers on. Sends one short SysEx query to every port.");
        btn(connectB, "Connect", [this] { connect(); }, "Open the selected ports and read the unit's state");
        statusLab.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(statusLab);

        // clock
        lab(clockLab, "Clock source"); lab(rateLab, "Master sample rate");
        for (auto* c : {&clockBox, &rateBox}) addAndMakeVisible(*c);
        clockBox.addItem("Master (the Pulse)", 1); clockBox.addItem("ADAT", 2); clockBox.addItem("BNC", 3);
        for (int i = 0; i < 9; ++i) rateBox.addItem(rateNames[i], i + 1);
        clockBox.setTooltip("Only one device may be master. Master = the Pulse's own clock. ADAT and BNC need a signal or it shows no lock.");
        clockBox.onChange = [this] {
            if (guard) return;
            session.setClock(wm::pulse16::Clock(clockBox.getSelectedId() - 1));
            statusLab.setText(clockBox.getSelectedId() == 1 ? "Clock: Pulse is master" : "Clock follows an input. If nothing feeds it the Pulse shows no lock; switch back to Master.", juce::dontSendNotification);
        };
        rateBox.setTooltip("Sample rate the Pulse runs at when it is master");
        rateBox.onChange = [this] { if (guard) return; session.setRate(wm::pulse16::Rate(rateBox.getSelectedId() - 1)); };
        btn(resendRateB, "Resend", [this] { if (rateBox.getSelectedId() > 0) session.setRate(wm::pulse16::Rate(rateBox.getSelectedId() - 1)); },
            "The Pulse screen can show a rate it is not running. This sends the chosen rate again.");
        clockHint.setColour(juce::Label::textColourId, col::dim); clockHint.setFont(12.0f);
        clockHint.setText("Pulse on Master: set the audio interface to follow its ADAT input. Never set both to follow each other.", juce::dontSendNotification);
        addAndMakeVisible(clockHint);

        // headphones
        lab(hpLab, "Headphone volume"); lab(hpSrcLab, "Source"); lab(hpChLab, "Channel");
        hpVol.setSliderStyle(juce::Slider::LinearHorizontal); hpVol.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
        hpVol.setRange(0, 79, 1); hpVol.setColour(juce::Slider::thumbColourId, col::teal); hpVol.setTooltip("0 is the loudest (0 dB). The lowest position is Off.");
        hpVol.textFromValueFunction = [](double v) { return v < 1 ? juce::String("Off") : juce::String(int(v) - 79) + " dB"; };
        hpVol.onValueChange = [this] { if (!guard) session.sendHeadphoneVolume(int(hpVol.getValue())); };
        addAndMakeVisible(hpVol);
        const char* srcs[8] = {"Analog In", "Analog Out", "ADAT IN", "ADAT OUT", "MADI IN", "MADI OUT", "DANTE IN", "DANTE OUT"};
        for (int i = 0; i < 8; ++i) hpSrc.addItem(srcs[i], i + 1);
        hpSrc.onChange = [this] { if (!guard) session.sendHeadphoneSource(wm::pulse16::HpSource(hpSrc.getSelectedId() - 1)); };
        addAndMakeVisible(hpSrc);
        for (int i = 0; i < 17; ++i) hpCh.addItem(juce::String(i * 2 + 1) + "+" + juce::String(i * 2 + 2), i + 1);
        hpCh.onChange = [this] { if (!guard) sendHpChannel(); };
        addAndMakeVisible(hpCh);
        hpMono.setButtonText("Mono"); hpMono.onClick = [this] { if (!guard) sendHpChannel(); }; addAndMakeVisible(hpMono);
        hpMono.setTooltip("Mono listens to one channel. Only channels 1 to 34 have been checked.");

        // levels
        for (int i = 0; i < 16; ++i) {
            for (int o = 0; o < 2; ++o) {
                auto& s = o == 0 ? inGain[size_t(i)] : outLvl[size_t(i)];
                s.setSliderStyle(juce::Slider::LinearVertical); s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 40, 16);
                s.setColour(juce::Slider::thumbColourId, o == 0 ? col::teal : col::orange);
                if (o == 0) { s.setRange(-8, 20, 1); s.setDoubleClickReturnValue(true, 20); s.setTooltip("Input reference level in dBu. Higher = more headroom (less gain)."); }
                else { s.setRange(-8, 20, 1); s.setDoubleClickReturnValue(true, 20); s.setTooltip("Output reference level in dBu."); }
                s.onValueChange = [this, i, o, &s] {
                    if (guard) return;
                    if (o == 0) session.setInputGain(i + 1, wm::pulse16::dbuToInputGain(int(s.getValue())));
                    else session.setOutputLevel(i + 1, wm::pulse16::dbuToOutputLevel(int(s.getValue())));
                };
                addAndMakeVisible(s);
            }
        }

        // presets
        for (int i = 0; i < 6; ++i) {
            btn(loadB[size_t(i)], ("Load " + juce::String(i + 1)).toRawUTF8(), [this, i] { session.loadPresetSlot(i + 1); statusLab.setText("Loading preset " + juce::String(i + 1) + "...", juce::dontSendNotification); }, "Recall this preset stored in the Pulse");
            btn(saveB[size_t(i)], ("Save " + juce::String(i + 1)).toRawUTF8(), [this, i] { confirmSave(i + 1); }, "Store the Pulse's current settings in this slot (overwrites it)");
        }

        // unit settings
        lab(brLab, "Display brightness");
        bright.setSliderStyle(juce::Slider::LinearHorizontal); bright.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20); bright.setRange(0, 15, 1);
        bright.setColour(juce::Slider::thumbColourId, col::teal); bright.setTooltip("0 darkest, 15 brightest");
        bright.onValueChange = [this] { if (!guard) session.sendBrightness(int(bright.getValue())); };
        addAndMakeVisible(bright);
        keyClick.setButtonText("Key click"); keyClick.onClick = [this] { if (!guard) session.sendKeyClick(keyClick.getToggleState()); }; addAndMakeVisible(keyClick);
        keyClick.setToggleState(true, juce::dontSendNotification);
        metersTgl.setButtonText("Signal lights (experimental)"); addAndMakeVisible(metersTgl);
        metersTgl.setTooltip("Shows a light per channel when the unit reports signal. Only near-silent captures exist, so the scale and channel order are unverified.");
        metersTgl.onClick = [this] { repaint(lvlR); };
        btn(backupB, "Backup...", [this] { backupToFile(); }, "Save gains, levels, routing, clock, rate and headphone settings to a file");
        btn(restoreB, "Restore...", [this] { restoreFromFile(); }, "Load a backup file and send it to the Pulse");

        unitNote.setColour(juce::Label::textColourId, col::dim); unitNote.setFont(11.5f);
        unitNote.setText("The Pulse does not report brightness or key click, so these only show what you set here.", juce::dontSendNotification);
        addAndMakeVisible(unitNote);
        setControlsEnabled(false);
        refreshPorts();
        statusLab.setText("Not connected", juce::dontSendNotification);
    }
    ~PulseView() override { link.close(); }

    // Starts a session without opening ports (replaying a capture in tests and screenshots).
    void beginReplay() { session.connect(juce::Time::getMillisecondCounter()); }

    // Everything the Pulse sends ends up here. Also usable to replay a captured connect (tests, screenshots).
    void feedSysEx(const wm::pulse16::Bytes& m) {
        session.onSysEx(m);
        const bool isMeter = m.size() == 46 && m[9] == 0x04;
        if (session.connected() && session.haveState() && !controlsOn) { statusLab.setText("Connected", juce::dontSendNotification); setControlsEnabled(true); }
        if (isMeter) { if (metersTgl.getToggleState()) repaint(lvlR); return; }    // 4.5 polls a second: do not rebuild every control
        refreshFromState();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        drawCard(g, conn, "Connection"); drawCard(g, clockR, "Clock"); drawCard(g, hpR, "Headphones");
        drawCard(g, lvlR, "Levels (dBu)"); drawCard(g, routeR, "Routing  (click a square: that output block takes that input block)");
        drawCard(g, presetR, "Presets (stored in the Pulse)");  drawCard(g, unitR, "Unit settings");
        {   // levels: headings, divider and channel numbers
            int sw = (lvlR.getWidth() - 16) / 32, x0 = lvlR.getX() + 8, mid = x0 + 16 * sw;
            g.setFont(12.5f);
            g.setColour(col::teal); g.drawText("INPUT GAIN  1-16", x0, lvlR.getY() + 5, 16 * sw, 18, juce::Justification::centred);
            g.setColour(col::orange); g.drawText("OUTPUT LEVEL  1-16", mid, lvlR.getY() + 5, 16 * sw, 18, juce::Justification::centred);
            g.setColour(col::dim.withAlpha(0.7f)); g.fillRect(float(mid) - 1.0f, float(lvlR.getY() + 8), 2.0f, float(lvlR.getHeight() - 16));
            if (metersTgl.getToggleState() && session.state().metersKnown) {   // experimental signal lights, order unverified
                for (int i = 0; i < 32; ++i) {
                    bool on = wm::pulse16::signalPresent(session.state().meters[i]);
                    g.setColour(on ? (i < 16 ? col::teal : col::orange) : juce::Colour(0xff3a3a42));
                    g.fillEllipse(float(x0 + i * sw + sw / 2 - 3), float(lvlR.getY() + 25), 6.0f, 6.0f);
                }
            }
            g.setColour(col::dim); g.setFont(11.0f);
            for (int i = 0; i < 32; ++i) g.drawText(juce::String(i % 16 + 1), x0 + i * sw, lvlR.getBottom() - 15, sw, 14, juce::Justification::centred);
        }
        // routing grid: 14 output blocks (rows) by 14 input blocks (columns). A row takes exactly one source.
        const int lw = kLabW, hh = kHeadH, cw = cellW(), rh = kRowH;
        for (int c = 0; c < 14; ++c) {
            juce::String n(wm::pulse16::kInputBlockNames[c]); int sp = n.indexOfChar(' ');
            g.setColour(c < 6 ? col::title : col::dim); g.setFont(10.0f);
            g.drawFittedText(n.substring(0, sp) + "\n" + n.substring(sp + 1), routeR.getX() + lw + c * cw, routeR.getY() + 26, cw, hh - 4, juce::Justification::centred, 2);
        }
        for (int r = 0; r < 14; ++r) {
            int y = routeR.getY() + 26 + hh + r * rh;
            g.setColour(r < 6 ? col::text : col::dim); g.setFont(11.5f); g.drawText(wm::pulse16::kOutputBlockNames[r], routeR.getX() + 12, y, lw - 12, rh, juce::Justification::centredLeft);
            for (int c = 0; c < 14; ++c) {
                auto cell = juce::Rectangle<int>(routeR.getX() + lw + c * cw, y, cw, rh).reduced(2);
                bool on = session.haveState() && session.state().routeCol[r] == c;
                g.setColour(on ? col::teal : juce::Colour(0xff34343c)); g.fillRoundedRectangle(cell.toFloat(), 3);
            }
        }
    }
    void resized() override {
        auto r = getLocalBounds().reduced(10);
        conn = r.removeFromTop(64); r.removeFromTop(8);
        auto row2 = r.removeFromTop(96); r.removeFromTop(8);
        clockR = row2.removeFromLeft(row2.getWidth() / 2 - 4); row2.removeFromLeft(8); hpR = row2;
        lvlR = r.removeFromTop(176); r.removeFromTop(8);
        routeR = r.removeFromTop(26 + kHeadH + 14 * kRowH + 12); r.removeFromTop(8);
        auto row3 = r.removeFromTop(104);
        presetR = row3.removeFromLeft(row3.getWidth() * 3 / 5); row3.removeFromLeft(8); unitR = row3;

        auto c = conn.reduced(12, 0).withTrimmedTop(26);
        inLab.setBounds(c.removeFromLeft(48)); inBox.setBounds(c.removeFromLeft(170).reduced(0, 4)); c.removeFromLeft(8);
        outLab.setBounds(c.removeFromLeft(58)); outBox.setBounds(c.removeFromLeft(170).reduced(0, 4)); c.removeFromLeft(8);
        findB.setBounds(c.removeFromLeft(96).reduced(2, 4)); connectB.setBounds(c.removeFromLeft(80).reduced(2, 4)); backupB.setBounds(c.removeFromLeft(86).reduced(2, 4)); restoreB.setBounds(c.removeFromLeft(86).reduced(2, 4)); c.removeFromLeft(10);
        statusLab.setBounds(c);

        auto k = clockR.reduced(12, 0).withTrimmedTop(24);
        auto k1 = k.removeFromTop(28);
        clockLab.setBounds(k1.removeFromLeft(90)); clockBox.setBounds(k1.removeFromLeft(150).reduced(0, 2)); k1.removeFromLeft(10);
        rateLab.setBounds(k1.removeFromLeft(112)); rateBox.setBounds(k1.removeFromLeft(96).reduced(0, 2)); k1.removeFromLeft(6); resendRateB.setBounds(k1.removeFromLeft(130).reduced(0, 2));
        clockHint.setBounds(k.removeFromTop(36));

        auto h = hpR.reduced(12, 0).withTrimmedTop(24);
        auto h1 = h.removeFromTop(28); hpLab.setBounds(h1.removeFromLeft(110)); hpVol.setBounds(h1);
        auto h2 = h.removeFromTop(30); hpSrcLab.setBounds(h2.removeFromLeft(50)); hpSrc.setBounds(h2.removeFromLeft(120).reduced(0, 2)); h2.removeFromLeft(10);
        hpChLab.setBounds(h2.removeFromLeft(60)); hpCh.setBounds(h2.removeFromLeft(80).reduced(0, 2)); h2.removeFromLeft(8); hpMono.setBounds(h2.removeFromLeft(80));

        auto l = lvlR.reduced(8, 0).withTrimmedTop(34);
        int sw = l.getWidth() / 32;
        l.removeFromBottom(14);
        for (int i = 0; i < 16; ++i) inGain[size_t(i)].setBounds(l.getX() + i * sw, l.getY(), sw, l.getHeight());
        for (int i = 0; i < 16; ++i) outLvl[size_t(i)].setBounds(l.getX() + (16 + i) * sw, l.getY(), sw, l.getHeight());

        auto p = presetR.reduced(12, 0).withTrimmedTop(28).withTrimmedBottom(8);
        int bw = p.getWidth() / 6; auto top = p.removeFromTop(p.getHeight() / 2);
        for (int i = 0; i < 6; ++i) { loadB[size_t(i)].setBounds(top.removeFromLeft(bw).reduced(2)); saveB[size_t(i)].setBounds(p.removeFromLeft(bw).reduced(2)); }

        auto u = unitR.reduced(12, 0).withTrimmedTop(24);
        auto u1 = u.removeFromTop(26); brLab.setBounds(u1.removeFromLeft(120)); bright.setBounds(u1);
        auto u2 = u.removeFromTop(26); keyClick.setBounds(u2.removeFromLeft(110)); metersTgl.setBounds(u2);
        unitNote.setBounds(u.removeFromTop(34));
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!session.haveState()) return;
        const int lw = kLabW, hh = kHeadH, cw = cellW(), rh = kRowH;
        auto p = e.getPosition(); int c = (p.x - routeR.getX() - lw) / cw, r = (p.y - routeR.getY() - 26 - hh) / rh;
        if (p.x < routeR.getX() + lw || p.y < routeR.getY() + 26 + hh || c < 0 || c > 13 || r < 0 || r > 13) return;
        session.setRoute(r, c); repaint(routeR);
    }

private:
    static constexpr int kLabW = 130, kHeadH = 32, kRowH = 20;
    int cellW() const { return juce::jmax(40, (routeR.getWidth() - kLabW - 16) / 14); }
    static constexpr const char* rateNames[9] = {"32 kHz", "44.1 kHz", "48 kHz", "64 kHz", "88.2 kHz", "96 kHz", "128 kHz", "176.4 kHz", "192 kHz"};

    void refreshPorts() {
        inBox.clear(juce::dontSendNotification); outBox.clear(juce::dontSendNotification); ins = juce::MidiInput::getAvailableDevices(); outs = juce::MidiOutput::getAvailableDevices();
        for (int i = 0; i < ins.size(); ++i) inBox.addItem(ins[i].name, i + 1);
        for (int i = 0; i < outs.size(); ++i) outBox.addItem(outs[i].name, i + 1);
        for (int i = 0; i < ins.size(); ++i) if (ins[i].name.containsIgnoreCase("UM-ONE")) inBox.setSelectedId(i + 1, juce::dontSendNotification);
        for (int i = 0; i < outs.size(); ++i) if (outs[i].name.containsIgnoreCase("UM-ONE")) outBox.setSelectedId(i + 1, juce::dontSendNotification);
    }
    bool openSelected() {
        int i = inBox.getSelectedId() - 1, o = outBox.getSelectedId() - 1;
        return i >= 0 && o >= 0 && link.open(ins[i].identifier, outs[o].identifier);
    }
    void connect() {
        if (!openSelected()) { statusLab.setText("Pick a MIDI in and out port", juce::dontSendNotification); return; }
        statusLab.setText("Waiting for the Pulse...", juce::dontSendNotification);
        session.connect(juce::Time::getMillisecondCounter()); startTimerHz(10);
    }
    // 10 Hz: runs the session clock (meter polling is also the keep-alive) and reports connection loss.
    void timerCallback() override {
        session.tick(juce::Time::getMillisecondCounter());
        switch (session.status()) {
            case wm::pulse16::Status::NoAnswer:
                link.close(); setControlsEnabled(false); stopTimer();
                statusLab.setText("No answer. Check the cables (MIDI in and out may be swapped) and that RemoteFish is closed.", juce::dontSendNotification); break;
            case wm::pulse16::Status::Lost:
                if (controlsOn) { setControlsEnabled(false); statusLab.setText("Connection lost. Press Connect to try again.", juce::dontSendNotification); } break;
            case wm::pulse16::Status::Connected:
                if (!controlsOn && session.haveState()) { setControlsEnabled(true); statusLab.setText("Connected", juce::dontSendNotification); }
                else if (controlsOn && statusLab.getText().startsWith("Connection lost")) statusLab.setText("Connected", juce::dontSendNotification);
                break;
            default: break;
        }
    }
    // Find: try each in/out pair with the same device name first, then wait for the probe answer.
    void findPulse() {
        refreshPorts(); scanList.clear();
        for (int i = 0; i < ins.size(); ++i) for (int o = 0; o < outs.size(); ++o) if (ins[i].name == outs[o].name) scanList.push_back({i, o});
        if (scanList.empty()) { statusLab.setText("No matching MIDI in/out pair found", juce::dontSendNotification); return; }
        scanIdx = 0; scanNext();
    }
    void scanNext() {
        if (scanIdx >= int(scanList.size())) { statusLab.setText("No Pulse answered", juce::dontSendNotification); link.close(); return; }
        auto [i, o] = scanList[size_t(scanIdx)];
        statusLab.setText("Trying " + ins[i].name + "...", juce::dontSendNotification);
        if (!link.open(ins[i].identifier, outs[o].identifier)) { ++scanIdx; scanNext(); return; }
        session.connect(juce::Time::getMillisecondCounter());
        juce::Timer::callAfterDelay(500, [safe = juce::Component::SafePointer<PulseView>(this), idx = scanIdx] {
            if (safe == nullptr || safe->scanIdx != idx) return;
            if (safe->session.connected()) { auto [ii, oo] = safe->scanList[size_t(idx)]; safe->inBox.setSelectedId(ii + 1, juce::dontSendNotification); safe->outBox.setSelectedId(oo + 1, juce::dontSendNotification);
                safe->scanIdx = 1 << 20; safe->startTimerHz(10); }
            else { ++safe->scanIdx; safe->scanNext(); }
        });
    }
    void sendHpChannel() {
        int pair = hpCh.getSelectedId() - 1; if (pair < 0) return;
        if (hpMono.getToggleState()) session.sendHeadphoneMono(pair * 2 + 1); else session.sendHeadphoneStereo(pair * 2 + 1);
    }
    void confirmSave(int n) {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Save preset " + juce::String(n))
            .withMessage("This overwrites preset " + juce::String(n) + " stored in the Pulse with its current settings.").withButton("Save").withButton("Cancel").withAssociatedComponent(this),
            [this, n](int result) { if (result == 1) session.savePresetSlot(n); });
    }
    void backupToFile() {
        chooser = std::make_unique<juce::FileChooser>("Save Pulse 16 backup", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("pulse16-backup.txt"), "*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (f == juce::File()) return;
            statusLab.setText(f.replaceWithText(wm::pulse16::toText(session.state())) ? "Backup saved: " + f.getFileName() : "Could not write " + f.getFileName(), juce::dontSendNotification);
        });
    }
    void restoreFromFile() {
        chooser = std::make_unique<juce::FileChooser>("Restore Pulse 16 backup", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fc.getResult(); if (!f.existsAsFile()) return;
            wm::pulse16::Pulse16State b;
            if (!wm::pulse16::fromText(f.loadFileAsString().toStdString(), b)) { statusLab.setText("Not a valid Pulse 16 backup file", juce::dontSendNotification); return; }
            juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Restore backup")
                .withMessage("This sends the saved gains, levels, routing, clock, sample rate and headphone settings to the Pulse.").withButton("Restore").withButton("Cancel").withAssociatedComponent(this),
                [this, b](int result) { if (result == 1) { int n = session.applyState(b); statusLab.setText("Restored (" + juce::String(n) + " settings sent)", juce::dontSendNotification); refreshFromState(); } });
        });
    }
    void setControlsEnabled(bool on) {
        controlsOn = on;
        for (juce::Component* c : std::initializer_list<juce::Component*>{&clockBox, &rateBox, &resendRateB, &hpVol, &hpSrc, &hpCh, &hpMono, &bright, &keyClick, &metersTgl, &backupB}) c->setEnabled(on);
        restoreB.setEnabled(on);
        for (auto& s : inGain) s.setEnabled(on);
        for (auto& s : outLvl) s.setEnabled(on);
        for (auto& b : loadB) b.setEnabled(on);
        for (auto& b : saveB) b.setEnabled(on);
    }
    void refreshFromState() {
        const auto& st = session.state(); guard = true;
        if (st.clockKnown) clockBox.setSelectedId(int(st.clock) + 1, juce::dontSendNotification);
        if (st.rateKnown) rateBox.setSelectedId(int(st.rate) + 1, juce::dontSendNotification);
        if (st.settingsKnown) {
            hpVol.setValue(st.hpVolumeVv, juce::dontSendNotification); hpSrc.setSelectedId(st.hpSource + 1, juce::dontSendNotification);
            int first = 1; bool stereo = true;
            if (wm::pulse16::decodeHeadphoneChannel(uint8_t(st.hpChannelCode), first, stereo)) { hpCh.setSelectedId((first - 1) / 2 + 1, juce::dontSendNotification); hpMono.setToggleState(!stereo, juce::dontSendNotification); }
        }
        if (session.haveState()) {
            for (int i = 0; i < 16; ++i) { inGain[size_t(i)].setValue(wm::pulse16::inputGainToDbu(st.inputGainDb[i]), juce::dontSendNotification); outLvl[size_t(i)].setValue(wm::pulse16::outputLevelToDbu(st.outputLevelDb[i]), juce::dontSendNotification); }
        }
        guard = false; repaint();
    }

    PulseLink link; wm::pulse16::Session session;
    juce::Array<juce::MidiDeviceInfo> ins, outs; std::vector<std::pair<int, int>> scanList; int scanIdx = 0; bool guard = false, controlsOn = false;
    juce::Rectangle<int> conn, clockR, hpR, lvlR, routeR, presetR, unitR;
    juce::Label unitNote, inLab, outLab, statusLab, clockLab, rateLab, clockHint, hpLab, hpSrcLab, hpChLab, brLab;
    juce::ComboBox inBox, outBox, clockBox, rateBox, hpSrc, hpCh;
    juce::TextButton findB, connectB, resendRateB, backupB, restoreB; juce::ToggleButton metersTgl; std::unique_ptr<juce::FileChooser> chooser; std::array<juce::TextButton, 6> loadB, saveB;
    juce::Slider hpVol, bright; juce::ToggleButton hpMono, keyClick; std::array<juce::Slider, 16> inGain, outLvl;
};

}  // namespace wmui
