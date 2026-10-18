// MIDI hub page: device cards (name, icon, colour, channels, clock, log filters), snapshots of the device list,
// live MIDI log from the real MIDI inputs, and the anonymised debug report. Model: src/mixer/MidiHub.h.
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <memory>
#include "../mixer/MidiHub.h"
#include "MixerView.h"

namespace wmui {

// Opens every MIDI input, turns messages into log entries on the message thread.
class MidiMonitor : public juce::MidiInputCallback {
public:
    explicit MidiMonitor(wm::mixer::MidiHub& h) : hub(h), alive(std::make_shared<std::atomic<bool>>(true)) {}
    ~MidiMonitor() override { *alive = false; close(); }
    void open() {
        close();
        for (auto& d : juce::MidiInput::getAvailableDevices())
            if (auto in = juce::MidiInput::openDevice(d.identifier, this)) { in->start(); inputs.push_back(std::move(in)); }
    }
    void close() { for (auto& i : inputs) i->stop(); inputs.clear(); }
    static std::vector<std::string> ports() {
        std::vector<std::string> v; for (auto& d : juce::MidiInput::getAvailableDevices()) v.push_back(d.name.toStdString()); return v;
    }
    void handleIncomingMidiMessage(juce::MidiInput* src, const juce::MidiMessage& m) override {
        std::string port = src ? src->getName().toStdString() : "?";
        juce::MidiMessage copy(m);
        auto flag = alive;
        juce::MessageManager::callAsync([this, flag, port, copy] { if (*flag) log(port, copy); });
    }
private:
    static std::string hex(const juce::MidiMessage& m, int maxBytes) {
        std::string s; auto* d = m.getRawData(); int n = juce::jmin(m.getRawDataSize(), maxBytes);
        for (int i = 0; i < n; ++i) { char b[8]; std::snprintf(b, sizeof b, "%s%02X", i ? " " : "", d[i]); s += b; }
        return s;
    }
    void log(const std::string& port, const juce::MidiMessage& m) {
        const wm::mixer::MidiDevice* dev = nullptr;
        for (auto& d : hub.devices()) if (d.inPort == port) dev = &d;
        wm::mixer::LogEntry e;
        auto t = juce::Time::getCurrentTime();
        e.time = t.formatted("%H:%M:%S").toStdString() + "." + juce::String(t.getMilliseconds()).paddedLeft('0', 3).toStdString();
        e.route = port; e.dir = "IN";
        std::string who = dev && !dev->name.empty() ? dev->name : port;
        if (m.isNoteOn() || m.isNoteOff()) {
            e.kind = wm::mixer::LogKind::Note; e.type = "Note";
            e.text = who + ": " + (m.isNoteOn() ? "on " : "off ") + juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 3).toStdString() +
                     " vel " + std::to_string(m.getVelocity()) + " ch " + std::to_string(m.getChannel());
        } else if (m.isController()) {
            if (dev && !dev->fCC) return;
            e.kind = wm::mixer::LogKind::CC; e.type = "CC";
            e.text = who + ": CC " + std::to_string(m.getControllerNumber()) + " = " + std::to_string(m.getControllerValue()) + " ch " + std::to_string(m.getChannel());
        } else if (m.isProgramChange()) {
            if (dev && !dev->fPC) return;
            e.type = "PC"; e.text = who + ": program " + std::to_string(m.getProgramChangeNumber()) + " ch " + std::to_string(m.getChannel());
            if (m.getChannel() == 16 && hub.recallByProgram(m.getProgramChangeNumber())) { e.text += " (hub snapshot recalled)"; if (onRecalled) onRecalled(); }
        } else if (m.isSysEx()) {
            if (dev && !dev->fSysEx) return;
            e.kind = wm::mixer::LogKind::SysEx; e.type = "SysEx"; e.text = who + ": " + hex(m, 24);
        } else if (m.isMidiClock()) {
            e.kind = wm::mixer::LogKind::Clock; e.type = "Clock";
            if (++clockTicks[port] % 24 != 0) return;   // one line per beat, not 24
            e.text = who + ": clock (1 beat)";
        } else if (m.isMidiStart() || m.isMidiStop() || m.isMidiContinue()) {
            if (dev && !dev->fTransport) return;
            e.type = "Transport"; e.text = who + (m.isMidiStart() ? ": start" : m.isMidiStop() ? ": stop" : ": continue");
        } else if (m.isPitchWheel()) { e.type = "Pitch"; e.text = who + ": pitch " + std::to_string(m.getPitchWheelValue()); }
        else if (m.isAftertouch() || m.isChannelPressure()) { e.type = "Pressure"; e.text = who + ": pressure"; }
        else return;
        hub.push(e);
    }
    wm::mixer::MidiHub& hub; std::shared_ptr<std::atomic<bool>> alive;
    std::vector<std::unique_ptr<juce::MidiInput>> inputs; std::map<std::string, int> clockTicks;
public:
    std::function<void()> onRecalled;
};

class LogView : public juce::Component {
public:
    std::vector<wm::mixer::LogEntry> rows;
    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        g.setFont(12.5f);
        int y = 0;
        for (auto& e : rows) {
            if (y + 20 > getHeight()) break;
            if (e.warn) { g.setColour(juce::Colour(0xff3a2a20)); g.fillRect(0, y, getWidth(), 20); }
            g.setColour(col::dim); g.drawText(e.time, 8, y, 92, 20, juce::Justification::centredLeft);
            g.setColour(e.dir == "OUT" ? col::orange : col::mint); g.drawText(e.dir, 104, y, 36, 20, juce::Justification::centredLeft);
            g.setColour(col::title); g.drawText(e.type, 144, y, 70, 20, juce::Justification::centredLeft);
            g.setColour(e.warn ? col::orange : col::text); g.drawText(e.text, 218, y, getWidth() - 226, 20, juce::Justification::centredLeft);
            y += 20;
        }
        if (rows.empty()) { g.setColour(col::dim); g.drawText("No MIDI yet. Move a knob or play a key on a connected device.", getLocalBounds(), juce::Justification::centred); }
    }
};

class MidiPage : public juce::Component, private juce::Timer {
public:
    std::function<void()> onChanged;   // device list changed: save it
    MidiPage(wm::mixer::MidiHub& h) : hub(h), monitor(h) {
        monitor.onRecalled = [this] { selectDevice(juce::jmin(sel, int(hub.devices().size()) - 1)); rebuildCards(); if (onChanged) onChanged(); };
        auto btn = [this](juce::TextButton& b, const char* t, const char* tip) { b.setButtonText(t); b.setTooltip(tip); addAndMakeVisible(b); };
        btn(rescanB, "Rescan", "Look for new MIDI ports and open them for the log");
        rescanB.onClick = [this] { rescan(); };
        auto ed = [this](juce::TextEditor& t, const char* tip) { t.setTooltip(tip); t.setSelectAllWhenFocused(true); addAndMakeVisible(t); };
        ed(nameE, "Device name (shown in the log)"); ed(inE, "MIDI input port"); ed(outE, "MIDI output port"); ed(loE, "First MIDI channel 1..16"); ed(hiE, "Last MIDI channel 1..16");
        for (auto& e : emojiList()) emojiC.addItem(e.empty() ? juce::String("(no icon)") : juce::String(juce::CharPointer_UTF8(e.c_str())), emojiC.getNumItems() + 1);
        addAndMakeVisible(emojiC); addAndMakeVisible(sw);
        sw.onPick = [this](int i) { edit.colour = i; };
        btn(singleB, "Single", "One MIDI channel"); btn(multiB, "Multi", "A range of MIDI channels");
        singleB.setClickingTogglesState(true); multiB.setClickingTogglesState(true);
        singleB.setRadioGroupId(11); multiB.setRadioGroupId(11);
        singleB.onClick = [this] { edit.multi = false; layoutEdit(); }; multiB.onClick = [this] { edit.multi = true; layoutEdit(); };
        const char* cn[3] = {"Receive", "Send", "Off"};
        for (int i = 0; i < 3; ++i) {
            btn(clockB[i], cn[i], "Clock mode of this device (saved with the device)"); clockB[i].setClickingTogglesState(true); clockB[i].setRadioGroupId(12);
            clockB[i].onClick = [this, i] { edit.clock = i; };
        }
        auto tg = [this](juce::ToggleButton& t, const char* n, const char* tip, bool wm::mixer::MidiDevice::*f) {
            t.setButtonText(n); t.setTooltip(tip); t.onClick = [this, &t, f] { edit.*f = t.getToggleState(); }; addAndMakeVisible(t); };
        tg(pcT, "Program Change", "Show program changes in the log", &wm::mixer::MidiDevice::fPC);
        tg(ccT, "CC", "Show controller messages in the log", &wm::mixer::MidiDevice::fCC);
        tg(trT, "Transport", "Show start/stop/continue in the log", &wm::mixer::MidiDevice::fTransport);
        tg(sxT, "SysEx", "Show system exclusive messages in the log", &wm::mixer::MidiDevice::fSysEx);
        btn(saveB, "Save", "Apply the changes to this device"); saveB.setColour(juce::TextButton::buttonColourId, col::pink.withAlpha(0.8f));
        saveB.onClick = [this] { commit(); };
        btn(revertB, "Revert", "Discard changes"); revertB.onClick = [this] { selectDevice(sel); };
        btn(forgetB, "Forget device", "Remove this device card"); forgetB.onClick = [this] { hub.forget(sel); sel = juce::jmax(0, sel - 1); rebuildCards(); selectDevice(sel); if (onChanged) onChanged(); };
        snapName.setText("Studio", false); snapName.setTooltip("Name for a new snapshot"); addAndMakeVisible(snapName);
        btn(storeB, "Store", "Store the device list under this name; recall it with a Program Change on channel 16");
        storeB.onClick = [this] { auto n = snapName.getText().toStdString(); if (!n.empty()) { hub.store(n); rebuildSnaps(); if (onChanged) onChanged(); } };
        const char* fn[5] = {"All", "Notes", "CC", "SysEx", "Clock"};
        for (int i = 0; i < 5; ++i) {
            btn(filtB[i], fn[i], "Filter the log"); filtB[i].setClickingTogglesState(true); filtB[i].setRadioGroupId(13);
            filtB[i].onClick = [this, i] { filter = i; refreshLog(true); };
        }
        filtB[0].setToggleState(true, juce::dontSendNotification);
        btn(pauseB, "Pause", "Freeze the log display"); pauseB.setClickingTogglesState(true); pauseB.setColour(juce::TextButton::buttonOnColourId, col::orange);
        btn(fullB, "Full log", "Show more rows"); fullB.setClickingTogglesState(true); fullB.onClick = [this] { resized(); };
        btn(saveLogB, "Save log...", "Write the log to a text file");
        saveLogB.onClick = [this] { chooser = std::make_unique<juce::FileChooser>("Save MIDI log", juce::File(), "*.txt");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
                auto f = c.getResult(); if (f == juce::File()) return; juce::String s;
                for (auto& e : hub.filtered(0)) s << e.time << " " << e.dir << " " << e.type << " " << e.text << "\n";
                f.replaceWithText(s); }); };
        btn(clearB, "Clear", "Empty the log"); clearB.onClick = [this] { hub.clearLog(); refreshLog(true); };
        addAndMakeVisible(logView); addAndMakeVisible(shareNote);
        const char* on[8] = {"Remove user, host, IP", "Remove paths, project names", "Hash serial numbers", "Channel names to IDs", "Truncate SysEx", "Last 200 MIDI log lines", "Audio settings, dropouts", "Crash traces"};
        for (int i = 0; i < 8; ++i) { optT[i].setButtonText(on[i]); optT[i].setToggleState(true, juce::dontSendNotification); optT[i].setTooltip("Include or strip this in the report");
            optT[i].onClick = [this] { rep.removeUser = optT[0].getToggleState(); rep.removePaths = optT[1].getToggleState(); rep.hashSerials = optT[2].getToggleState();
                rep.channelIds = optT[3].getToggleState(); rep.truncateSysEx = optT[4].getToggleState(); rep.lastLog = optT[5].getToggleState();
                rep.audio = optT[6].getToggleState(); rep.crash = optT[7].getToggleState(); };
            addAndMakeVisible(optT[i]); }
        btn(prevB, "Preview report", "Show exactly what would be saved"); prevB.onClick = [this] { preview.setVisible(!preview.isVisible()); preview.setText(report(), false); resized(); };
        btn(zipB, "Save .zip", "Save the report as a zip you can send yourself");
        zipB.setColour(juce::TextButton::buttonColourId, col::teal); zipB.setColour(juce::TextButton::textColourOffId, col::bg);
        zipB.onClick = [this] { chooser = std::make_unique<juce::FileChooser>("Save debug report", juce::File(), "*.zip");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
                auto f = c.getResult(); if (f == juce::File()) return; auto txt = report();
                juce::ZipFile::Builder b;
                JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS   // the raw-pointer overload exists in every JUCE 8 release
                b.addEntry(new juce::MemoryInputStream(txt.toRawUTF8(), txt.getNumBytesAsUTF8(), true), 9, "whiskermix-report.txt", juce::Time::getCurrentTime());
                JUCE_END_IGNORE_DEPRECATION_WARNINGS
                f.deleteFile(); juce::FileOutputStream out(f); if (out.openedOk()) b.writeToStream(out, nullptr); }); };
        btn(copyB, "Copy summary", "Copy the report text to the clipboard"); copyB.onClick = [this] { juce::SystemClipboard::copyTextToClipboard(report()); };
        preview.setMultiLine(true); preview.setReadOnly(true); preview.setVisible(false); addAndMakeVisible(preview);
        rescan(); startTimerHz(5);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(col::bg);
        drawCard(g, devCard, "Devices . " + juce::String(int(hub.devices().size())), col::title);
        drawCard(g, editCard, "Edit device", col::title);
        if (!hub.devices().empty() && sel < int(hub.devices().size())) { g.setColour(stripColour(edit.colour)); g.setFont(15.0f); g.drawText(juce::String(edit.name), editCard.getX() + 110, editCard.getY() + 4, 300, 20, juce::Justification::centredLeft); }
        drawCard(g, snapCard, "MIDI snapshots", col::title);
        drawCard(g, logCard, "Live MIDI log", col::title);
        drawCard(g, repCard, "Export debug report . anonymous", col::title);
        g.setColour(col::dim); g.setFont(12.0f);
        g.drawText("Name", labX, nameE.getY(), 80, 24, juce::Justification::centredLeft); g.drawText("Emoji", labX, emojiC.getY(), 80, 24, juce::Justification::centredLeft);
        g.drawText("In port", labX, inE.getY(), 80, 24, juce::Justification::centredLeft); g.drawText("Out port", labX2, outE.getY(), 80, 24, juce::Justification::centredLeft);
        g.drawText("Clock", labX, clockB[0].getY(), 80, 24, juce::Justification::centredLeft); g.drawText("Log shows", labX2, pcT.getY(), 80, 24, juce::Justification::centredLeft);
        g.drawText("Colour", labX2, sw.getY(), 80, 24, juce::Justification::centredLeft); g.drawText("MIDI channel", labX2, singleB.getY(), 90, 24, juce::Justification::centredLeft);
        g.drawText("Keeps the last 5000 messages. Clock shows one line per beat. Nothing is sent anywhere automatically.", logCard.getX() + 130, logCard.getY() + 6, logCard.getWidth() - 140, 18, juce::Justification::centredRight);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        devRows = rowsFor(r.getWidth() - 24);
        devCard = r.removeFromTop(devRows * 36 + 70); r.removeFromTop(8);
        editCard = r.removeFromTop(236); r.removeFromTop(8);
        snapCard = r.removeFromTop(58); r.removeFromTop(8);
        int logH = fullB.getToggleState() ? 460 : 230;
        logCard = r.removeFromTop(logH + 90); r.removeFromTop(8);
        repCard = r.removeFromTop(preview.isVisible() ? 340 : 150);
        // devices
        auto d = devCard.reduced(12); d.removeFromTop(22);
        rescanB.setBounds(devCard.getRight() - 90, devCard.getY() + 6, 78, 24);
        layoutCards(d);
        // edit
        auto e = editCard.reduced(12); e.removeFromTop(26);
        int half = e.getWidth() / 2; labX = e.getX(); labX2 = e.getX() + half + 12;
        auto L = e.removeFromLeft(half - 6); auto R = e; R.removeFromLeft(18);
        auto rowL = [&L]() { auto x = L.removeFromTop(30); L.removeFromTop(4); return x.withTrimmedLeft(80); };
        auto rowR = [&R]() { auto x = R.removeFromTop(30); R.removeFromTop(4); return x.withTrimmedLeft(88); };
        nameE.setBounds(rowL()); emojiC.setBounds(rowL().removeFromLeft(160)); inE.setBounds(rowL());
        { auto c = rowL(); int w = c.getWidth() / 3; for (int i = 0; i < 3; ++i) clockB[i].setBounds(c.removeFromLeft(w).reduced(1)); }
        { auto c = rowR(); sw.setBounds(c.removeFromLeft(9 * 28)); }
        { auto c = rowR(); singleB.setBounds(c.removeFromLeft(70).reduced(1)); multiB.setBounds(c.removeFromLeft(70).reduced(1)); c.removeFromLeft(8); loE.setBounds(c.removeFromLeft(44)); c.removeFromLeft(14); hiE.setBounds(c.removeFromLeft(44)); }
        outE.setBounds(rowR());
        { auto c = rowR(); int w = c.getWidth() / 4; pcT.setBounds(c.removeFromLeft(w + 20)); ccT.setBounds(c.removeFromLeft(w - 20)); trT.setBounds(c.removeFromLeft(w + 10)); sxT.setBounds(c); }
        { auto c = editCard.reduced(12).removeFromBottom(30); saveB.setBounds(c.removeFromLeft(70).reduced(1)); revertB.setBounds(c.removeFromLeft(80).reduced(1)); forgetB.setBounds(c.removeFromLeft(130).reduced(1)); }
        layoutEdit();
        // snapshots
        auto s = snapCard.reduced(12); s.removeFromTop(18);
        snapName.setBounds(s.removeFromRight(120).reduced(0, 1)); storeB.setBounds(s.removeFromRight(70).reduced(2, 1)); s.removeFromRight(4);
        for (size_t i = 0; i < snapB.size(); ++i) snapB[i]->setBounds(s.removeFromLeft(100).reduced(2, 1));
        // log
        auto l = logCard.reduced(12); auto bar = l.removeFromTop(28 + 20); bar.removeFromTop(20);
        pauseB.setBounds(bar.removeFromLeft(70).reduced(2)); fullB.setBounds(bar.removeFromLeft(80).reduced(2)); saveLogB.setBounds(bar.removeFromLeft(100).reduced(2)); clearB.setBounds(bar.removeFromLeft(70).reduced(2));
        for (int i = 4; i >= 0; --i) filtB[i].setBounds(bar.removeFromRight(62).reduced(2));
        logView.setBounds(l.removeFromTop(logH));
        shareNote.setBounds(l.removeFromTop(30).reduced(0, 4));
        // report
        auto p = repCard.reduced(12); p.removeFromTop(24);
        auto grid = p.removeFromTop(56); int cw = grid.getWidth() / 4;
        for (int i = 0; i < 8; ++i) optT[i].setBounds(grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * 28, cw - 4, 26);
        p.removeFromTop(6); auto rowb = p.removeFromTop(32);
        prevB.setBounds(rowb.removeFromLeft(130).reduced(1)); zipB.setBounds(rowb.removeFromLeft(100).reduced(1)); copyB.setBounds(rowb.removeFromLeft(130).reduced(1));
        preview.setBounds(p.withTrimmedTop(6));
    }
    void rescanPorts() { rescan(); }
    void refresh() { rebuildCards(); selectDevice(juce::jmin(sel, int(hub.devices().size()) - 1)); rebuildSnaps(); }
    int neededHeight() const { return 8 + (devRows * 36 + 70) + 8 + 236 + 8 + 58 + 8 + ((fullB.getToggleState() ? 460 : 230) + 90) + 8 + (preview.isVisible() ? 340 : 150) + 8; }
private:
    int rowsFor(int width) const {
        int x = 0, rows = 1;
        for (auto& c : cardB) {
            int w = 28 + 8 * c->getButtonText().length();
            if (x + w > width && x > 0) { x = 0; ++rows; }
            x += w + 8;
        }
        return rows;
    }
    void layoutCards(juce::Rectangle<int> d) {
        int x = d.getX(), y = d.getY();
        for (auto& c : cardB) {
            int w = 28 + 8 * c->getButtonText().length();
            if (x + w > d.getRight() && x > d.getX()) { x = d.getX(); y += 36; }
            c->setBounds(x, y, w, 30); x += w + 8;
        }
    }
    void layoutEdit() {
        bool m = edit.multi; hiE.setVisible(m);
        singleB.setToggleState(!m, juce::dontSendNotification); multiB.setToggleState(m, juce::dontSendNotification);
    }
    void rebuildCards() {
        cardB.clear();
        for (size_t i = 0; i < hub.devices().size(); ++i) {
            auto& d = hub.devices()[i];
            auto b = std::make_unique<juce::TextButton>(juce::String(juce::CharPointer_UTF8(d.emoji.c_str())) + (d.emoji.empty() ? "" : " ") + juce::String(d.name));
            b->setClickingTogglesState(true); b->setRadioGroupId(10);
            b->setColour(juce::TextButton::buttonOnColourId, stripColour(d.colour));
            b->setColour(juce::TextButton::textColourOnId, col::bg);
            b->setColour(juce::TextButton::textColourOffId, d.online ? col::text : col::dim);
            b->setTooltip(d.online ? "Click to edit" : "Offline: not currently connected");
            int idx = int(i); b->onClick = [this, idx] { selectDevice(idx); };
            addAndMakeVisible(*b); cardB.push_back(std::move(b));
        }
        resized();
        if (getWidth() > 0 && getHeight() != neededHeight()) setSize(getWidth(), neededHeight());
    }
    void selectDevice(int i) {
        if (hub.devices().empty()) { sel = 0; return; }
        sel = juce::jlimit(0, int(hub.devices().size()) - 1, i); edit = hub.devices()[size_t(sel)];
        nameE.setText(edit.name, false); inE.setText(edit.inPort, false); outE.setText(edit.outPort, false);
        loE.setText(juce::String(edit.chLo), false); hiE.setText(juce::String(edit.chHi), false);
        emojiC.setSelectedId(emojiIndex(edit.emoji) + 1, juce::dontSendNotification); sw.value = edit.colour; sw.repaint();
        for (int k = 0; k < 3; ++k) clockB[k].setToggleState(edit.clock == k, juce::dontSendNotification);
        pcT.setToggleState(edit.fPC, juce::dontSendNotification); ccT.setToggleState(edit.fCC, juce::dontSendNotification);
        trT.setToggleState(edit.fTransport, juce::dontSendNotification); sxT.setToggleState(edit.fSysEx, juce::dontSendNotification);
        if (sel < int(cardB.size())) cardB[size_t(sel)]->setToggleState(true, juce::dontSendNotification);
        layoutEdit(); repaint();
    }
    void commit() {
        if (hub.devices().empty()) return;
        edit.name = nameE.getText().toStdString(); edit.inPort = inE.getText().toStdString(); edit.outPort = outE.getText().toStdString();
        edit.chLo = juce::jlimit(1, 16, loE.getText().getIntValue()); edit.chHi = juce::jlimit(edit.chLo, 16, hiE.getText().getIntValue());
        if (!edit.multi) edit.chHi = edit.chLo;
        edit.emoji = emojiList()[size_t(juce::jmax(0, emojiC.getSelectedId() - 1))];
        hub.devices()[size_t(sel)] = edit; rebuildCards(); selectDevice(sel); if (onChanged) onChanged();
    }
    void rescan() {
        auto ports = MidiMonitor::ports(); hub.rescan(ports); monitor.open();
        rebuildCards(); selectDevice(sel); rebuildSnaps(); if (onChanged) onChanged();
    }
    void rebuildSnaps() {
        snapB.clear();
        for (auto& n : hub.snapshotNames()) {
            auto b = std::make_unique<juce::TextButton>(n); b->setTooltip("Recall this snapshot");
            b->onClick = [this, n] { hub.recall(n); rebuildCards(); selectDevice(sel); if (onChanged) onChanged(); };
            addAndMakeVisible(*b); snapB.push_back(std::move(b));
        }
        resized();
    }
    juce::String report() const {
        return juce::String(hub.debugReport(rep, "WhiskerMix 0.1.0", juce::SystemStats::getOperatingSystemName().toStdString(), ""));
    }
    void refreshLog(bool force) {
        if (pauseB.getToggleState() && !force) return;
        logView.rows = hub.filtered(filter);
        int maxRows = fullB.getToggleState() ? 23 : 11;
        if (int(logView.rows.size()) > maxRows) logView.rows.resize(size_t(maxRows));
        logView.repaint();
        auto w = hub.lastWarning();
        shareNote.setText(w.empty() ? "" : "! " + w, juce::dontSendNotification);
    }
    void timerCallback() override { refreshLog(false); }

    wm::mixer::MidiHub& hub; MidiMonitor monitor; wm::mixer::MidiDevice edit; int sel = 0, filter = 0, devRows = 1, labX = 0, labX2 = 0;
    wm::mixer::ReportOptions rep;
    juce::Rectangle<int> devCard, editCard, snapCard, logCard, repCard;
    std::vector<std::unique_ptr<juce::TextButton>> cardB, snapB;
    juce::TextButton rescanB, singleB, multiB, clockB[3], saveB, revertB, forgetB, storeB, filtB[5], pauseB, fullB, saveLogB, clearB, prevB, zipB, copyB;
    juce::TextEditor nameE, inE, outE, loE, hiE, snapName, preview; juce::ComboBox emojiC; Swatches sw;
    juce::ToggleButton pcT, ccT, trT, sxT, optT[8]; LogView logView; juce::Label shareNote;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Scrollable wrapper so the long page fits small windows.
class MidiView : public juce::Component {
public:
    explicit MidiView(wm::mixer::MidiHub& h) : page(h) {
        vp.setViewedComponent(&page, false); vp.setScrollBarsShown(true, false); addAndMakeVisible(vp);
    }
    MidiPage page;
    void refresh() { page.refresh(); resized(); }
    void rescan() { page.rescanPorts(); resized(); }
    void resized() override { vp.setBounds(getLocalBounds()); page.setSize(juce::jmax(700, vp.getWidth() - vp.getScrollBarThickness()), page.neededHeight()); }
private:
    juce::Viewport vp;
};

}  // namespace wmui
