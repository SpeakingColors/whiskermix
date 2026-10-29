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
#include "MockKit.h"

namespace wmui {

class PulseView : public juce::Component, private juce::Timer {
public:
    PulseView() : session(link) {
        link.onSysEx = [this](const wm::pulse16::Bytes& m) { feedSysEx(m); };
        link.onSent = [this](const wm::pulse16::Bytes& m) { logSysEx("OUT", m); };
        auto lab = [this](juce::Label& l, const char* t) { l.setText(t, juce::dontSendNotification); l.setColour(juce::Label::textColourId, col::dim); l.setFont(12.5f); addAndMakeVisible(l); };
        auto btn = [this](juce::TextButton& b, const char* t, std::function<void()> f, const char* tip) { b.setButtonText(t); b.setTooltip(tip); b.onClick = std::move(f); addAndMakeVisible(b); };

        lab(inLab, "MIDI in"); lab(outLab, "MIDI out");
        addAndMakeVisible(inBox); addAndMakeVisible(outBox);
        btn(findB, "Find Pulse", [this] { findPulse(); }, "Tries each MIDI port pair and keeps the one the Pulse answers on. Sends one short SysEx query to every port.");
        btn(connectB, "Connect", [this] { connect(); }, "Open the selected ports and read the unit's state");
        statusLab.setColour(juce::Label::textColourId, col::dim); addAndMakeVisible(statusLab);

        // header band: status pills, Connect / Unplug cable
        unplugB.setButtonText("Unplug cable"); unplugB.setTooltip("Close the MIDI ports and stop talking to the Pulse (nothing is changed on the unit)");
        unplugB.onClick = [this] { link.close(); session.disconnect(); setControlsEnabled(false); stopTimer(); statusLab.setText("Not connected", juce::dontSendNotification); repaint(); };
        addAndMakeVisible(unplugB);
        // clock source as three buttons (the combo below stays as the model and is hidden)
        { const char* cn[3] = {"Master", "ADAT", "BNC"}; for (int i = 0; i < 3; ++i) { ckB[i].setButtonText(cn[i]); ckB[i].setTooltip("Clock source: only one device may be master"); ckB[i].setClickingTogglesState(false);
            ckB[i].onClick = [this, i] { clockBox.setSelectedId(i + 1, juce::sendNotification); syncClockButtons(); }; addAndMakeVisible(ckB[i]); } }
        // clock
        lab(clockLab, "Clock source"); lab(rateLab, "Master sample rate");
        addChildComponent(clockBox); addAndMakeVisible(rateBox);
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
        addAndMakeVisible(hpVol); hpVol.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
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
                if (o == 0) { s.setRange(0, 28, 1); s.setDoubleClickReturnValue(true, 0); s.setTextValueSuffix(""); s.textFromValueFunction = [](double v) { return juce::String(20 - int(v)); }; s.setTooltip("Input gain, 0 to +28 dB. The number is the input level in dBu (20 minus gain). Fader up = more gain, like RemoteFish."); }
                else { s.setRange(-8, 20, 1); s.setDoubleClickReturnValue(true, 20); s.setTooltip("Output reference level in dBu."); }
                s.setValue(o == 0 ? 0 : 20, juce::dontSendNotification); s.updateText();
                s.onValueChange = [this, i, o, &s] {
                    if (guard) return;
                    if (o == 0) session.setInputGain(i + 1, int(s.getValue()));
                    else session.setOutputLevel(i + 1, wm::pulse16::dbuToOutputLevel(int(s.getValue())));
                };
                addAndMakeVisible(s);
            }
        }

        // presets
        for (int i = 0; i < 6; ++i) {
            btn(loadB[size_t(i)], "Load", [this, i] { session.loadPresetSlot(i + 1); statusLab.setText("Loading preset " + juce::String(i + 1) + "...", juce::dontSendNotification); }, "Recall this preset stored in the Pulse");
            btn(saveB[size_t(i)], "Save", [this, i] { confirmSave(i + 1); }, "Store the Pulse's current settings in this slot (overwrites it)");
        }

        // unit settings
        lab(brLab, "Display brightness");
        bright.setSliderStyle(juce::Slider::LinearHorizontal); bright.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20); bright.setRange(0, 15, 1);
        bright.setColour(juce::Slider::thumbColourId, col::teal); bright.setTooltip("Higher = brighter display (the unit takes 0 as brightest, so the value is flipped when sent)");
        bright.onValueChange = [this] { if (!guard) session.sendBrightness(15 - int(bright.getValue())); };
        addAndMakeVisible(bright);
        keyClick.setButtonText("Key click"); keyClick.onClick = [this] { if (!guard) session.sendKeyClick(keyClick.getToggleState()); }; addAndMakeVisible(keyClick);
        keyClick.setToggleState(true, juce::dontSendNotification);
        metersTgl.setButtonText("Signal lights (experimental)"); addAndMakeVisible(metersTgl);
        metersTgl.setTooltip("Shows a light per channel when the unit reports signal. Only near-silent captures exist, so the scale and channel order are unverified.");
        metersTgl.onClick = [this] { repaint(lvlR); };
        btn(backupB, "To file...", [this] { backupToFile(); }, "Save gains, levels, routing, clock, rate and headphone settings to a file");
        btn(restoreB, "From file...", [this] { restoreFromFile(); }, "Load a backup file and send it to the Pulse");
        btn(backupNowB, "Backup now", [this] { backupText.setText(juce::String(wm::pulse16::toText(session.state())), false); }, "Write the unit's settings as text below");
        btn(copyBackB, "Copy", [this] { juce::SystemClipboard::copyTextToClipboard(backupText.getText()); }, "Copy the backup text");
        btn(restoreTxtB, "Restore", [this] {
            wm::pulse16::Pulse16State b; if (!wm::pulse16::fromText(backupText.getText().toStdString(), b)) { statusLab.setText("Not a valid Pulse 16 backup text", juce::dontSendNotification); return; }
            juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Restore backup").withMessage("This sends the pasted gains, levels, routing, clock, sample rate and headphone settings to the Pulse.").withButton("Restore").withButton("Cancel").withAssociatedComponent(this),
                [this, b](int r) { if (r == 1) { int n = session.applyState(b); statusLab.setText("Restored (" + juce::String(n) + " settings sent)", juce::dontSendNotification); refreshFromState(); } }); }, "Send the pasted backup text to the unit");
        backupText.setMultiLine(true); backupText.setTooltip("Backup text: press Backup now, or paste a saved backup and press Restore"); addAndMakeVisible(backupText);
        btn(logToggle, juce::CharPointer_UTF8("\xE2\x96\xB8 SysEx log"), [this] { logOpen = !logOpen; logToggle.setButtonText(juce::String(juce::CharPointer_UTF8(logOpen ? "\xE2\x96\xBE SysEx log" : "\xE2\x96\xB8 SysEx log"))); setSize(getWidth(), neededHeight()); resized(); }, "Show the SysEx messages sent to and from the unit");
        btn(logClear, "Clear", [this] { logLines.clear(); repaint(); }, "Empty the log"); btn(logCopy, "Copy", [this] { juce::SystemClipboard::copyTextToClipboard(logLines.joinIntoString("\n")); }, "Copy the log text");
        pollsTgl.setButtonText("show meter polls"); pollsTgl.setTooltip("The unit is asked for meter data several times a second; hidden unless this is on"); addAndMakeVisible(pollsTgl);

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
        logSysEx("IN ", m);
        session.onSysEx(m);
        const bool isMeter = m.size() == 46 && m[9] == 0x04;
        if (session.connected() && session.haveState() && !controlsOn) { statusLab.setText(connectedText(), juce::dontSendNotification); setControlsEnabled(true); }
        if (isMeter) { if (metersTgl.getToggleState()) repaint(lvlR); return; }    // 4.5 polls a second: do not rebuild every control
        refreshFromState();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        const juce::Colour mute(0xffa8a59d), ink(0xffeceae4);
        // header band
        g.setColour(juce::Colour(0xff34373e)); g.fillRoundedRectangle(conn.toFloat(), 8.0f); g.setColour(col::line); g.drawRoundedRectangle(conn.toFloat().reduced(0.5f), 8.0f, 1.0f);
        { juce::Font f(20.0f, juce::Font::bold); f.setExtraKerningFactor(0.08f); g.setFont(f); g.setColour(ink); g.drawText("PULSE 16", conn.getX() + 12, conn.getY() + 5, 130, 30, juce::Justification::centredLeft); }
        g.setFont(juce::Font(10.0f)); g.setColour(mute); g.drawText("Ferrofish Pulse 16 DX over MIDI SysEx", conn.getX() + 140, conn.getY() + 10, 220, 20, juce::Justification::centredLeft);
        bool on = session.status() == wm::pulse16::Status::Connected;
        auto pill = [&](juce::Rectangle<int> r, const juce::String& t, bool dot, juce::Colour dc, bool mono) {
            g.setColour(juce::Colour(0xff1f2125)); g.fillRoundedRectangle(r.toFloat(), 11.0f); g.setColour(col::line); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 11.0f, 1.0f);
            if (dot) { g.setColour(dc); g.fillEllipse(float(r.getX() + 9), float(r.getCentreY() - 3), 6.0f, 6.0f); }
            g.setColour(ink); g.setFont(mono ? juce::Font(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::plain) : juce::Font(10.0f)); g.drawText(t, r.withTrimmedLeft(dot ? 18 : 10), juce::Justification::centredLeft, false); };
        pill({conn.getX() + 368, conn.getY() + 9, 120, 22}, on ? "Connected" : session.status() == wm::pulse16::Status::Lost ? "Connection lost" : "Not connected", true, on ? col::teal : juce::Colour(0xff6a6f7a), false);
        pill({conn.getX() + 498, conn.getY() + 9, 150, 22}, "unit ID: " + (on ? juce::String(wm::pulse16::idText(session.unitId())) : juce::String("not read yet")), false, col::teal, true);
        g.setColour(mute); g.setFont(juce::Font(11.0f)); g.drawText("MIDI in", conn.getX() + 12, conn.getY() + 44, 48, 28, juce::Justification::centredLeft); g.drawText("MIDI out", conn.getX() + 254, conn.getY() + 44, 56, 28, juce::Justification::centredLeft);
        g.setFont(juce::Font(10.5f)); g.drawText(infoText, infoR, juce::Justification::topLeft, true);
        drawCard(g, clockR, "Clock and sample rate"); drawCard(g, hpR, "Headphones");
        g.setColour(mute); g.setFont(juce::Font(9.0f)); g.drawText("CLOCK SOURCE", clockR.getX() + 12, clockR.getY() + 26, 100, 12, juce::Justification::centredLeft, false); g.drawText("MASTER SAMPLE RATE", clockR.getX() + 216, clockR.getY() + 26, 140, 12, juce::Justification::centredLeft, false);
        g.drawText("SOURCE", hpR.getX() + 12, hpR.getY() + 26, 80, 12, juce::Justification::centredLeft, false); g.drawText("CHANNEL", hpR.getX() + 150, hpR.getY() + 26, 80, 12, juce::Justification::centredLeft, false); g.drawText("VOLUME", hpR.getX() + 12, hpR.getY() + 62, 80, 12, juce::Justification::centredLeft, false);
        g.setColour(juce::Colour(0xff1f2125)); g.setFont(juce::Font(10.0f)); g.setColour(mute); g.drawText(clockHintTxt, clockR.getX() + 12, clockR.getY() + 76, clockR.getWidth() - 24, 28, juce::Justification::topLeft, true);
        drawCard(g, lvlR, "");
        {   // levels: headings, divider and channel numbers
            int sw = (lvlR.getWidth() - 16) / 32, x0 = lvlR.getX() + 8, mid = x0 + 16 * sw;
            juce::Font hf(12.0f, juce::Font::bold); hf.setExtraKerningFactor(0.08f); g.setFont(hf);
            g.setColour(col::teal); g.drawText("INPUT GAIN 1-16", x0 + 4, lvlR.getY() + 8, 16 * sw, 16, juce::Justification::centredLeft);
            g.setColour(col::orange); g.drawText("OUTPUT LEVEL 1-16", mid + 8, lvlR.getY() + 8, 16 * sw, 16, juce::Justification::centredLeft);
            g.setColour(mute); g.setFont(juce::Font(9.0f)); g.drawText("input dBu = 20 - gain, double-click resets", x0, lvlR.getY() + 9, 16 * sw - 6, 14, juce::Justification::centredRight, false);
            g.drawText("output dBu = 20 + level, double-click resets", mid, lvlR.getY() + 9, 16 * sw - 6, 14, juce::Justification::centredRight, false);
            g.setColour(juce::Colour(0xff4a4e57)); g.fillRect(float(mid) - 1.0f, float(lvlR.getY() + 30), 1.0f, float(lvlR.getHeight() - 60));
            if (metersTgl.getToggleState() && session.state().metersKnown) {   // experimental signal lights, order unverified
                for (int i = 0; i < 32; ++i) {
                    bool lit = wm::pulse16::signalPresent(session.state().meters[i]);
                    g.setColour(lit ? (i < 16 ? col::teal : col::orange) : juce::Colour(0xff3a3a42));
                    g.fillEllipse(float(x0 + i * sw + sw / 2 - 3), float(lvlR.getY() + 28), 6.0f, 6.0f);
                }
            }
            g.setColour(mute); g.setFont(juce::Font(9.0f));
            for (int i = 0; i < 32; ++i) g.drawText(juce::String(i % 16 + 1), x0 + i * sw, lvlR.getBottom() - 16, sw, 12, juce::Justification::centred);
        }
        // routing grid: 14 output blocks (rows) by 14 input blocks (columns, headers rotated). A row takes exactly one source.
        drawCard(g, routeR, "Routing - rows are outputs, columns are inputs - one source per row");
        const int lw = kLabW, hh = kHeadH, cw = cellW(), rh = kRowH;
        for (int c = 0; c < 14; ++c) {
            g.setColour(c < 6 ? ink : mute); g.setFont(juce::Font(8.5f));
            g.saveState(); g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, 0, 0).translated(float(routeR.getX() + lw + c * cw + cw / 2 + 4), float(routeR.getY() + 28 + hh - 2)));
            g.drawText(wm::pulse16::kInputBlockNames[c], 0, -6, hh - 4, 12, juce::Justification::centredLeft, false); g.restoreState();
        }
        for (int r = 0; r < 14; ++r) {
            int y = routeR.getY() + 28 + hh + r * rh;
            g.setColour(r < 6 ? ink : mute); g.setFont(juce::Font(9.5f)); g.drawText(wm::pulse16::kOutputBlockNames[r], routeR.getX() + 12, y, lw - 12, rh, juce::Justification::centredLeft);
            for (int c = 0; c < 14; ++c) {
                auto cell = juce::Rectangle<int>(routeR.getX() + lw + c * cw, y, cw, rh).reduced(2);
                bool lit = session.haveState() && session.state().routeCol[r] == c;
                g.setColour(lit ? col::teal : juce::Colour(0xff1f2125)); g.fillRoundedRectangle(cell.toFloat(), 3);
            }
        }
        g.setColour(mute); g.setFont(juce::Font(9.5f)); g.drawText("Click a square: that output block takes that input block. Rows 14 to 21 exist in the unit's state (probably Dante) and are not shown.", routeR.getX() + 12, routeR.getBottom() - 30, routeR.getWidth() - 24, 26, juce::Justification::topLeft, true);
        drawCard(g, presetR, "Presets - stored in the Pulse"); drawCard(g, unitR, "Unit settings"); drawCard(g, backR, "Backup and restore");
        { int bw = (presetR.getWidth() - 22) / 6; for (int i = 0; i < 6; ++i) { auto c = juce::Rectangle<int>(presetR.getX() + 11 + i * bw, presetR.getY() + 28, bw - 6, 64);
            g.setColour(juce::Colour(0xff2b2e34)); g.fillRoundedRectangle(c.toFloat(), 6.0f); g.setColour(col::line); g.drawRoundedRectangle(c.toFloat().reduced(0.5f), 6.0f, 1.0f);
            g.setColour(ink); g.setFont(juce::Font(16.0f, juce::Font::bold)); g.drawText(juce::String(i + 1), c.getX(), c.getY() + 3, c.getWidth(), 22, juce::Justification::centred, false); } }
        g.setColour(mute); g.setFont(juce::Font(9.5f)); g.drawText("Save overwrites the slot and asks you to confirm. Load makes the unit resend its state. Headphone volume is not part of a preset.", presetR.getX() + 11, presetR.getY() + 96, presetR.getWidth() - 22, 24, juce::Justification::topLeft, true);
        g.drawText("DISPLAY BRIGHTNESS", unitR.getX() + 11, unitR.getY() + 24, 140, 12, juce::Justification::centredLeft, false);
        g.drawText("The unit does not report brightness or key click, so these only show what you set here.", unitR.getX() + 11, unitR.getBottom() - 32, unitR.getWidth() - 22, 26, juce::Justification::topLeft, true);
        g.drawText("Backup writes gains, levels, routing, clock, rate and headphone settings as text. Paste a saved backup and press Restore to send it to the unit.", backR.getX() + 11, backR.getY() + 24, backR.getWidth() - 22, 24, juce::Justification::topLeft, true);
        // SysEx log bar
        g.setColour(juce::Colour(0xff34373e)); g.fillRoundedRectangle(logR.toFloat(), 8.0f); g.setColour(col::line); g.drawRoundedRectangle(logR.toFloat().reduced(0.5f), 8.0f, 1.0f);
        if (logOpen) {
            auto lr = logR.withTrimmedTop(46).reduced(10, 4); g.setColour(juce::Colour(0xff1f2125)); g.fillRoundedRectangle(lr.toFloat(), 4.0f);
            g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain)); int n = lr.getHeight() / 13;
            for (int i = 0; i < n && i < logLines.size(); ++i) { const auto& ln = logLines[logLines.size() - 1 - i]; g.setColour(ln.contains(" OUT ") ? col::orange : col::mint); g.drawText(ln, lr.getX() + 6, lr.getY() + 2 + i * 13, lr.getWidth() - 12, 13, juce::Justification::centredLeft, true); }
        }
    }
    void resized() override {
        auto r = getLocalBounds();
        const int W = r.getWidth(); int y = 0;
        conn = {0, y, W, 82}; y += 90; infoR = {2, y, W - 4, 28}; y += 34;
        clockR = {0, y, int(W * 0.55f) - 4, 112}; hpR = {clockR.getRight() + 8, y, W - clockR.getRight() - 8, 112}; y += 120;
        lvlR = {0, y, W, 214}; y += 222;
        routeR = {0, y, 530, 28 + kHeadH + 14 * kRowH + 36}; int rx = routeR.getRight() + 8, rw = W - rx;
        presetR = {rx, y, rw, 126}; unitR = {rx, y + 134, rw, 112}; backR = {rx, y + 254, rw, juce::jmax(150, routeR.getHeight() - 254)};
        y += routeR.getHeight() + 8; logR = {0, y, W, logOpen ? 190 : 44};

        { auto c = juce::Rectangle<int>(conn.getRight() - 12 - 90 - 8 - 70, conn.getY() + 8, 160, 24); connectB.setBounds(c.removeFromLeft(70)); c.removeFromLeft(8); unplugB.setBounds(c); }
        inBox.setBounds(conn.getX() + 62, conn.getY() + 48, 180, 24); outBox.setBounds(conn.getX() + 312, conn.getY() + 48, 180, 24);
        findB.setBounds(conn.getX() + 504, conn.getY() + 48, 90, 24); backupB.setBounds(juce::Rectangle<int>(0, 0, 0, 0)); restoreB.setBounds(juce::Rectangle<int>(0, 0, 0, 0));
        statusLab.setBounds(conn.getX() + 604, conn.getY() + 44, conn.getWidth() - 616, 32);
        inLab.setBounds({}); outLab.setBounds({});
        for (int i = 0; i < 3; ++i) ckB[i].setBounds(clockR.getX() + 12 + i * 64, clockR.getY() + 40, 62, 26);
        rateBox.setBounds(clockR.getX() + 216, clockR.getY() + 40, 110, 26); resendRateB.setBounds(clockR.getX() + 334, clockR.getY() + 40, 84, 26); clockLab.setBounds({}); rateLab.setBounds({}); clockHint.setBounds({});
        hpSrc.setBounds(hpR.getX() + 12, hpR.getY() + 40, 130, 24); hpCh.setBounds(hpR.getX() + 150, hpR.getY() + 40, 90, 24); hpMono.setBounds(hpR.getX() + 252, hpR.getY() + 40, 70, 24);
        hpVol.setBounds(hpR.getX() + 12, hpR.getY() + 76, hpR.getWidth() - 24, 24); hpLab.setBounds({}); hpSrcLab.setBounds({}); hpChLab.setBounds({});

        auto l = lvlR.reduced(8, 0).withTrimmedTop(36).withTrimmedBottom(18); int sw = l.getWidth() / 32;
        for (int i = 0; i < 16; ++i) inGain[size_t(i)].setBounds(l.getX() + i * sw, l.getY(), sw, l.getHeight());
        for (int i = 0; i < 16; ++i) outLvl[size_t(i)].setBounds(l.getX() + (16 + i) * sw, l.getY(), sw, l.getHeight());

        { int bw = (presetR.getWidth() - 22) / 6; for (int i = 0; i < 6; ++i) { auto c = juce::Rectangle<int>(presetR.getX() + 11 + i * bw, presetR.getY() + 28, bw - 6, 64); auto row = c.removeFromBottom(38).reduced(4, 0);
            loadB[size_t(i)].setBounds(row.removeFromTop(18)); saveB[size_t(i)].setBounds(row.withTrimmedTop(2)); } }
        brLab.setBounds({}); bright.setBounds(unitR.getX() + 11, unitR.getY() + 36, unitR.getWidth() - 22, 20); keyClick.setBounds(unitR.getX() + 11, unitR.getY() + 58, 100, 20); metersTgl.setBounds(unitR.getX() + 120, unitR.getY() + 58, unitR.getWidth() - 130, 20); unitNote.setBounds({});
        auto bk = backR.reduced(11, 0).withTrimmedTop(52); auto brow = bk.removeFromBottom(26); bk.removeFromBottom(6); backupText.setBounds(bk);
        int bx = brow.getX(); for (auto* b : {&backupNowB, &copyBackB, &backupB, &restoreB, &restoreTxtB}) { int w = b == &backupNowB ? 78 : b == &copyBackB ? 50 : b == &restoreTxtB ? 62 : 76; b->setBounds(bx, brow.getY(), w, 26); bx += w + 6; }
        logToggle.setBounds(logR.getX() + 10, logR.getY() + 9, 96, 26); pollsTgl.setBounds(logR.getRight() - 360, logR.getY() + 9, 150, 26); logClear.setBounds(logR.getRight() - 190, logR.getY() + 9, 60, 26); logCopy.setBounds(logR.getRight() - 124, logR.getY() + 9, 60, 26);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (!session.haveState()) return;
        const int lw = kLabW, hh = kHeadH, cw = cellW(), rh = kRowH;
        auto p = e.getPosition(); int c = (p.x - routeR.getX() - lw) / cw, r = (p.y - routeR.getY() - 28 - hh) / rh;
        if (p.x < routeR.getX() + lw || p.y < routeR.getY() + 28 + hh || c < 0 || c > 13 || r < 0 || r > 13) return;
        session.setRoute(r, c); repaint(routeR);
    }
    int neededHeight() const { return 90 + 34 + 120 + 222 + (28 + kHeadH + 14 * kRowH + 36) + 8 + (logOpen ? 190 : 44) + 12; }

private:
    static constexpr int kLabW = 110, kHeadH = 84, kRowH = 19;
    int cellW() const { return juce::jmax(20, (routeR.getWidth() - kLabW - 12) / 14); }
    void syncClockButtons() { for (int i = 0; i < 3; ++i) ckB[i].setToggleState(clockBox.getSelectedId() == i + 1, juce::dontSendNotification); clockHintTxt = "Pulse on Master: set the audio interface to follow its ADAT input. Never set both to follow each other. With nothing feeding ADAT or BNC the Pulse shows no lock."; repaint(); }
    void logSysEx(const char* dir, const wm::pulse16::Bytes& m) {
        const bool poll = m.size() == 46 && m.size() > 9 && m[9] == 0x04;
        if (poll && !pollsTgl.getToggleState()) return;
        juce::String t = juce::Time::getCurrentTime().formatted("%H:%M:%S") + " " + dir + " "; for (size_t i = 0; i < m.size() && i < 40; ++i) t << juce::String::toHexString(int(m[i])).paddedLeft('0', 2).toUpperCase() << " "; if (m.size() > 40) t << "...";
        logLines.add(t); while (logLines.size() > 400) logLines.remove(0); if (logOpen) repaint(logR);
    }
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
                if (!controlsOn && session.haveState()) { setControlsEnabled(true); statusLab.setText(connectedText(), juce::dontSendNotification); }
                else if (controlsOn && statusLab.getText().startsWith("Connection lost")) statusLab.setText(connectedText(), juce::dontSendNotification);
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
    juce::String connectedText() const { return "Connected · unit ID " + juce::String(wm::pulse16::idText(session.unitId())); }
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
        const auto& st = session.state(); guard = true; syncClockButtons();
        if (st.clockKnown) clockBox.setSelectedId(int(st.clock) + 1, juce::dontSendNotification);
        if (st.rateKnown) rateBox.setSelectedId(int(st.rate) + 1, juce::dontSendNotification);
        if (st.settingsKnown) {
            hpVol.setValue(st.hpVolumeVv, juce::dontSendNotification); hpSrc.setSelectedId(st.hpSource + 1, juce::dontSendNotification);
            int first = 1; bool stereo = true;
            if (wm::pulse16::decodeHeadphoneChannel(uint8_t(st.hpChannelCode), first, stereo)) { hpCh.setSelectedId((first - 1) / 2 + 1, juce::dontSendNotification); hpMono.setToggleState(!stereo, juce::dontSendNotification); }
        }
        if (session.haveState()) {
            for (int i = 0; i < 16; ++i) { inGain[size_t(i)].setValue(st.inputGainDb[i], juce::dontSendNotification); outLvl[size_t(i)].setValue(wm::pulse16::outputLevelToDbu(st.outputLevelDb[i]), juce::dontSendNotification); }
        }
        guard = false; repaint();
    }

    PulseLink link; wm::pulse16::Session session;
    juce::Array<juce::MidiDeviceInfo> ins, outs; std::vector<std::pair<int, int>> scanList; int scanIdx = 0; bool guard = false, controlsOn = false;
    juce::Rectangle<int> conn, clockR, hpR, lvlR, routeR, presetR, unitR, backR, logR, infoR; bool logOpen = false; juce::StringArray logLines; juce::String clockHintTxt, infoText = "Nothing is sent until you change a control. The page shows what the unit reported on connect; the unit's own screen can disagree with what it is really running.";
    juce::TextButton unplugB, ckB[3], backupNowB, copyBackB, restoreTxtB, logToggle, logClear, logCopy; juce::TextEditor backupText; juce::ToggleButton pollsTgl;
    juce::Label unitNote, inLab, outLab, statusLab, clockLab, rateLab, clockHint, hpLab, hpSrcLab, hpChLab, brLab;
    juce::ComboBox inBox, outBox, clockBox, rateBox, hpSrc, hpCh;
    juce::TextButton findB, connectB, resendRateB, backupB, restoreB; juce::ToggleButton metersTgl; std::unique_ptr<juce::FileChooser> chooser; std::array<juce::TextButton, 6> loadB, saveB;
    juce::Slider hpVol, bright; juce::ToggleButton hpMono, keyClick; std::array<juce::Slider, 16> inGain, outLvl;
};

}  // namespace wmui
