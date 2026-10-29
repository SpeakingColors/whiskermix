// Top-level view: toolbar (undo, redo, snapshots, save, load) + tabs (Mixer, EQ/Comp, Routing).
// Undo is automatic: when the state changes and no mouse drag is in progress, the previous state is pushed.
#pragma once
#include <fstream>
#include <sstream>
#include "MixerView.h"
#include "MockMixer.h"
#include "MockRouting.h"
#include "FxView.h"
#include "RoutingView.h"
#include "ReverbView.h"
#include "MidiView.h"
#include "PulseView.h"
#include "SettingsView.h"
#include "Look.h"
#include "../mixer/MidiHub.h"
#include "../mixer/Settings.h"
#include "../mixer/UndoStack.h"
#include "../mixer/StateJson.h"

namespace wmui {

// Gives the Pulse page a scrollbar when the window is shorter than the page.
class PulseHost : public juce::Component {
public:
    PulseHost(PulseView& p, juce::Viewport& v) : pulse(p), vp(v) {}
    void resized() override { vp.setBounds(getLocalBounds()); pulse.setSize(juce::jmax(900, getWidth() - 12), juce::jmax(getHeight(), pulse.neededHeight())); }
private:
    PulseView& pulse; juce::Viewport& vp;
};

class AppView : public juce::Component, private juce::Timer {
public:
    AppView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, wm::engine::AudioEngine& e)
        : mx(m), rt(r), mixerView(m, r, e), routing(m, r), reverb(m, [this] { eng.sync(); }), eng(e), midi(hub), settingsView(m, r) {
        loadPrefs();
        tabs.addTab("Mixer", col::panel, &mixerView, false);
        tabs.addTab("EQ / Comp", col::panel, &fxPage, false);
        tabs.addTab("Routing", col::panel, &routing, false);
        tabs.addTab("Reverb / Delay", col::panel, &reverb, false);
        tabs.addTab("MIDI hub", col::panel, &midi, false);
        pulseVp.setViewedComponent(&pulse, false); pulseVp.setScrollBarsShown(true, false); pulseHost.addAndMakeVisible(pulseVp);
        tabs.addTab("Pulse 16", col::panel, &pulseHost, false);
        tabs.addTab("Settings", col::panel, &settingsView, false);
        tabs.setTabBarDepth(40); tabs.setOutline(0);
        midi.page.onChanged = [this] { saveHub(); };
        settingsView.onChanged = [this] { mixerView.refreshAll(); routing.repaint(); };
        settingsView.onSettingsChanged = [this] { applyPrefs(); mixerView.refreshAll(); savePrefs(); };
        mixerView.onOpenFx = [this] { tabs.setCurrentTabIndex(1); };
        routing.onPrefs = [this] { savePrefs(); }; routing.devices = [this] { return audioDevices ? audioDevices() : std::vector<MockRoutingView::Dev>{}; };
        mixerView.addOutputPair = [this](int hw) { return addOutputPair(hw); };
        mixerView.onUndo = [this] { doUndo(); }; mixerView.onRedo = [this] { doRedo(); }; mixerView.onOpenRouting = [this] { tabs.setCurrentTabIndex(2); }; mixerView.onOpenReverb = [this] { tabs.setCurrentTabIndex(3); };
        mixerView.deviceStatus = [this] { return digifaceStatus ? digifaceStatus() : std::string("Digiface USB"); };
        addAndMakeVisible(tabs);
        auto btn = [this](juce::TextButton& b, const char* t, std::function<void()> f) { b.setButtonText(t); b.onClick = std::move(f); addAndMakeVisible(b); };
        btn(undoB, "Undo", [this] { if (undo.canUndo()) apply(undo.undo(current())); });
        btn(redoB, "Redo", [this] { if (undo.canRedo()) apply(undo.redo(current())); });
        btn(addB, "+ Channel", [this] { addChannel(); });
        btn(storeB, "Store", [this] { storeSnap(); });
        btn(recallB, "Recall", [this] { recallSnap(snapBox.getText().toStdString()); });
        snapBox.setEditableText(true); snapBox.setTextWhenNothingSelected("snapshot name");
        snapBox.onChange = [this] { if (snapBox.getSelectedId() > 0) recallSnap(snapBox.getText().toStdString()); };
        addAndMakeVisible(snapBox);
        dfLabel.setJustificationType(juce::Justification::centredRight); dfLabel.setFont(juce::Font(12.0f)); dfLabel.setColour(juce::Label::textColourId, col::text.withAlpha(0.8f));
        dfLabel.setTooltip("Digiface USB hardware mixer. Dry run: the same writes are made but nothing is sent (no device, no permission, or a test build)."); addAndMakeVisible(dfLabel);
        btn(saveB, "Save", [this] { save(); });
        btn(loadB, "Load", [this] { load(); });
        undoB.setTooltip("Undo the last change"); redoB.setTooltip("Redo"); addB.setTooltip("Add an input channel"); storeB.setTooltip("Store the whole mixer under the name in the snapshot box"); snapBox.setTooltip("Type a name and press Store; pick a name to recall it");
        recallB.setTooltip("Go back to the snapshot in the box"); saveB.setTooltip("Save the workspace to a .wmix file"); loadB.setTooltip("Open a .wmix workspace file");
        fxPage.eq.params = fxPage.comp.params = [this] { return &mx.state().channels[size_t(mixerView.selectedChannel)].fx; };
        fxPage.comp.grDb = [this] { return eng.gainReductionDb(mixerView.selectedChannel); };
        for (auto* b : {&fxPage.eqOn, &fxPage.compOn}) { b->setClickingTogglesState(true); addAndMakeVisible(b); fxPage.addAndMakeVisible(b); }
        fxPage.eqOn.setButtonText("EQ"); fxPage.compOn.setButtonText("COMP");
        fxPage.eqOn.onClick = [this] { fx().eqOn = fxPage.eqOn.getToggleState(); fxPage.repaint(); };
        fxPage.compOn.onClick = [this] { fx().compOn = fxPage.compOn.getToggleState(); fxPage.repaint(); };
        mixerView.onSelect = [this](int) { syncFx(); fxPage.repaint(); };
        last = sig();
        applyPrefs(); loadSnaps();
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<AppView>(this)] { if (safe != nullptr) { safe->midi.rescan(); safe->loadAutosave(); } });
        startTimerHz(30);
    }
    // Used by MockFrame (the mockup's frame): the old toolbar and tab bar are hidden, the frame drives them instead.
    void setChromeless(bool c) {
        chromeless = c; tabs.setTabBarDepth(c ? 0 : 40);
        for (auto* b : {&undoB, &redoB, &addB, &storeB, &recallB, &saveB, &loadB}) b->setVisible(!c);
        snapBox.setVisible(!c); dfLabel.setVisible(!c); resized(); repaint();
    }
    void showPage(int i) { static const int idx[5] = {0, 2, 4, 5, 6}; tabs.setCurrentTabIndex(idx[juce::jlimit(0, 4, i)]); }
    void doUndo() { if (undo.canUndo()) apply(undo.undo(current())); }
    void doRedo() { if (undo.canRedo()) apply(undo.redo(current())); }
    bool canUndo() const { return undo.canUndo(); }
    bool canRedo() const { return undo.canRedo(); }
    void doSave() { save(); }
    void doLoad() { load(); }
    std::function<void(int)> openPage;          // set by the frame: switch to a page (0 Mixer .. 4 Settings)
    std::function<void(bool)> onAlwaysOnTop;    // set by the app window
    // Options menu (the gear), like TotalMix FX: toggles, jumps to the settings sections, Reset Mix and Operational Mode.
    void showOptionsMenu(juce::Component* anchor) {
        auto& st = wm::mixer::settings(); juce::PopupMenu m, reset;
        m.addItem(1, "Deactivate screensaver", true, st.getOpt("noSaver", 0) > 0.5f);
        m.addItem(2, "Always on top", true, st.getOpt("alwaysTop", 0) > 0.5f);
        m.addSeparator();
        m.addItem(3, "Enable MIDI control", true, st.getOpt("rcMidi", 0) > 0.5f);
        m.addItem(4, "Enable OSC control", true, st.getOpt("rcOsc", 0) > 0.5f);
        m.addSeparator();
        m.addItem(10, "Preferences..."); m.addItem(11, "Settings..."); m.addItem(12, "Channel layout..."); m.addItem(13, "Key commands and remote...");
        static const char* rn[] = {"Straight playback with all to Main Out", "Straight playback", "Clear all submixes", nullptr, "Clear all channel effects", "Set output volumes", "Reset channel names", nullptr,
                                   "Set all channels mono", "Set all channels stereo", "Set inputs mono / outputs stereo", nullptr, "Total reset"};
        for (int i = 0; i < 13; ++i) { if (!rn[i]) reset.addSeparator(); else reset.addItem(100 + i, rn[i]); }
        m.addSubMenu("Reset mix", reset);
        m.addItem(20, "Operational mode...");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor), [this, safe = juce::Component::SafePointer<AppView>(this)](int r) {
            if (!safe || r <= 0) return;
            auto& s = wm::mixer::settings();
            auto go = [this](int sec) { if (openPage) openPage(4); settingsView.showSection(sec); };
            if (r == 1) { bool v = s.getOpt("noSaver", 0) < 0.5f; s.setOpt("noSaver", v); juce::Desktop::getInstance().setScreenSaverEnabled(!v); savePrefs(); }
            else if (r == 2) { bool v = s.getOpt("alwaysTop", 0) < 0.5f; s.setOpt("alwaysTop", v); if (onAlwaysOnTop) onAlwaysOnTop(v); savePrefs(); }
            else if (r == 3) { s.setOpt("rcMidi", s.getOpt("rcMidi", 0) < 0.5f); settingsView.syncSettings(); savePrefs(); }
            else if (r == 4) { s.setOpt("rcOsc", s.getOpt("rcOsc", 0) < 0.5f); settingsView.syncSettings(); savePrefs(); }
            else if (r == 10) go(2); else if (r == 11) go(3); else if (r == 12) go(0); else if (r == 13) go(4);
            else if (r >= 100 && r < 120) resetMix(r - 100);
            else if (r == 20) operationalMode();
        });
    }
    void operationalMode() {
        const bool daw = wm::mixer::settings().getOpt("opMode", 0) > 0.5f;
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon).withTitle("Operational mode")
            .withMessage(juce::String("Full mode: mixer active, all routing options available.\nDAW mode: straight playback routing, no input mix.\n\nNow: ") + (daw ? "DAW mode" : "Full mode") + ". Stored for now; the mode does not change the mixer yet.")
            .withButton("Full mode").withButton("DAW mode").withButton("Cancel"), [this](int r) {
                if (r == 1 || r == 2) { wm::mixer::settings().setOpt("opMode", r == 2 ? 1.0f : 0.0f); savePrefs(); } });
    }
    // Reset mix actions (each one can be undone).
    void resetMix(int what) {
        auto before = current(); auto& st = mx.state(); const int nc = int(st.channels.size()), nb = int(st.buses.size());
        auto splitAll = [this](bool toMono) {
            for (int c = int(mx.state().channels.size()) - 1; c >= 0; --c) {
                auto& ch = mx.state().channels[size_t(c)];
                if (toMono && ch.stereoLinked) { if (mx.splitStereo(c)) rt.duplicateInput(c); }
                else if (!toMono && !ch.stereoLinked && c + 1 < int(mx.state().channels.size()) && mx.joinStereo(c)) rt.eraseInput(c + 1);
            } };
        switch (what) {
            case 0: { rt.clear(); const int mi = wm::mixer::crMainIndex(st); for (int c = 0; c < nc; ++c) if (st.channels[size_t(c)].playback && mi >= 0) rt.set(c, mi, 0.0f); break; }
            case 1: { rt.clear(); for (int c = 0; c < nc; ++c) { auto& ch = st.channels[size_t(c)]; if (!ch.playback) continue;
                        for (int b = 0; b < nb; ++b) if (st.buses[size_t(b)].hwFirst == ch.hwFirst) { rt.set(c, b, 0.0f); break; } } break; }
            case 2: rt.clear(); break;
            case 4: for (auto& ch : st.channels) { ch.fx = {}; ch.chain = {}; } break;
            case 5: for (auto& b : st.buses) b.volumeDb = 0.0f; break;
            case 6: for (int c = 0; c < nc; ++c) st.channels[size_t(c)].name = "In " + std::to_string(c + 1); break;
            case 8: splitAll(true); break;
            case 9: splitAll(false); break;
            case 10: splitAll(true); for (auto& b : st.buses) b.stereo = true; break;
            case 12: { auto built = wm::mixer::buildFromLayout(wm::mixer::studioLayout()); mx.state() = built.mixer.state(); rt = built.routing; break; }
            default: return;
        }
        undo.push(before); last = sig(); eng.sync(); mixerView.refreshAll(); settingsView.rebuild(); routing.repaint(); reverb.refresh(); syncFx(); fxPage.repaint();
    }
    void showAbout() {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "WhiskerMix",
            "Studio hub: mixer for the RME Digiface USB, MIDI hub, Ferrofish Pulse 16 control. Free and open source. Digiface hardware mixer and Pulse 16 over USB MIDI; other interfaces are untested.");
    }
    juce::String workspaceName() const { return wsName.isEmpty() ? juce::String("Studio main") : wsName; }
    int currentSlot() const { return slot; }
    // Quick slots 1..5 are snapshots called "Slot n": click recalls, shift-click stores the current mixer there.
    void quickSlot(int i, bool store) {
        juce::String n = "Slot " + juce::String(i + 1);
        if (store) { snapBox.setText(n, juce::dontSendNotification); storeSnap(); slot = i; wsName = n; return; }
        for (auto& nm : mx.snapshotNames()) if (juce::String(nm) == n) { recallSnap(nm); slot = i; wsName = n; return; }
    }
    void showSnapshotMenu(juce::Component* anchor) {
        juce::PopupMenu m; int id = 1; auto names = mx.snapshotNames();
        for (auto& nm : names) m.addItem(id++, juce::String(nm), true, juce::String(nm) == wsName);
        if (!names.empty()) m.addSeparator();
        m.addItem(10000, "Store current as new snapshot");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(anchor), [this, names](int r) {
            if (r == 10000) { snapBox.setText("", juce::dontSendNotification); storeSnap(); }
            else if (r > 0 && r <= int(names.size())) { recallSnap(names[size_t(r - 1)]); wsName = juce::String(names[size_t(r - 1)]); }
        });
    }
    std::function<void(const std::string&)> onChannelAdded, onBusAdded;
    std::function<std::vector<MockRoutingView::Dev>()> audioDevices;   // Routing page device list (set by the app when the build can list devices)
    std::function<std::string()> digifaceStatus;   // shown at the right of the toolbar
    std::function<void()> onWorkspaceLoaded;        // the hardware mixer resends everything
    void paint(juce::Graphics& g) override {
        if (chromeless) { g.fillAll(juce::Colour(0xff2a2c31)); return; }
        g.fillAll(col::bg);
        // cat head logo + title
        auto lg = juce::Rectangle<float>(10, 4, 30, 28);
        juce::Path head; head.addEllipse(lg.getX() + 3, lg.getY() + 6, 24, 20);
        head.addTriangle(lg.getX() + 3, lg.getY() + 12, lg.getX() + 5, lg.getY() + 1, lg.getX() + 13, lg.getY() + 8);
        head.addTriangle(lg.getX() + 27, lg.getY() + 12, lg.getX() + 25, lg.getY() + 1, lg.getX() + 17, lg.getY() + 8);
        g.setColour(col::teal); g.strokePath(head, juce::PathStrokeType(2.0f));
        g.fillEllipse(lg.getX() + 10, lg.getY() + 14, 3, 3); g.fillEllipse(lg.getX() + 17, lg.getY() + 14, 3, 3);
        g.setColour(col::text); g.setFont(17.0f); g.drawText("WHISKERMIX", 48, 4, 130, 28, juce::Justification::centredLeft);
    }
    void resized() override {
        auto r = getLocalBounds();
        if (chromeless) { tabs.setBounds(r); return; }
        auto bar = r.removeFromTop(38).reduced(4); bar.removeFromLeft(180);
        for (auto* b : {&undoB, &redoB, &addB}) b->setBounds(bar.removeFromLeft(86).reduced(2));
        bar.removeFromLeft(10); snapBox.setBounds(bar.removeFromLeft(150).reduced(2)); storeB.setBounds(bar.removeFromLeft(64).reduced(2)); recallB.setBounds(bar.removeFromLeft(72).reduced(2));
        bar.removeFromLeft(10);
        for (auto* b : {&saveB, &loadB}) b->setBounds(bar.removeFromLeft(80).reduced(2));
        dfLabel.setBounds(bar.removeFromRight(juce::jmin(360, bar.getWidth())).reduced(2));
        tabs.setBounds(r);
    }
private:
    struct FxPage : juce::Component {
        EqGraph eq; CompGraph comp; juce::TextButton eqOn, compOn;
        FxPage() { addAndMakeVisible(eq); addAndMakeVisible(comp); }
        void paint(juce::Graphics& g) override { g.fillAll(col::bg); }
        void resized() override {
            auto r = getLocalBounds().reduced(10);
            auto top = r.removeFromTop(28);
            eqOn.setBounds(top.removeFromLeft(70).reduced(2)); compOn.setBounds(top.removeFromLeft(70).reduced(2));
            eq.setBounds(r.removeFromLeft(r.getWidth() * 3 / 5)); comp.setBounds(r.reduced(10, 0));
        }
    } fxPage;

    wm::engine::FxParams& fx() { return mx.state().channels[size_t(mixerView.selectedChannel)].fx; }
    void syncFx() { fxPage.eqOn.setToggleState(fx().eqOn, juce::dontSendNotification); fxPage.compOn.setToggleState(fx().compOn, juce::dontSendNotification); }
    static std::string fxSig(const wm::engine::FxParams& p) {
        std::ostringstream o;
        o << p.eqOn << p.compOn << ',' << p.lowCutHz << ',' << p.lowGain << ',' << p.lowFreq << ',' << p.midGain << ',' << p.midFreq << ',' << p.midQ
          << ',' << p.highGain << ',' << p.highFreq << ',' << p.threshDb << ',' << p.ratio << ',' << p.attackMs << ',' << p.releaseMs << ',' << p.makeupDb;
        return o.str();
    }
    std::string sig() const {
        std::string s = wm::mixer::toJson(mx.state(), rt);
        for (auto& c : mx.state().channels) s += fxSig(c.fx) + ';';
        for (auto& b : mx.state().buses) s += std::to_string(b.reverbOn) + std::to_string(b.delayOn) + std::to_string(int(b.reverb.type)) +
            std::to_string(b.reverb.p.decay) + std::to_string(b.reverb.p.dampHz) + std::to_string(b.reverb.p.preDelayMs) + std::to_string(b.reverb.p.size) +
            std::to_string(b.reverb.p.modDepth) + std::to_string(b.reverb.p.color) + std::to_string(b.reverb.shimmerSemis) + std::to_string(b.reverb.shimmerAmount) + std::to_string(b.reverb.shimmer2Semis) + std::to_string(b.reverb.shimmer2Amount) + std::to_string(b.reverb.wetHpHz) + std::to_string(b.reverb.wetLpHz) + std::to_string(b.reverb.mix) +
            std::to_string(b.delay.timeMs) + std::to_string(b.delay.feedback) + std::to_string(b.delay.shiftSemis) + std::to_string(b.delay.dampHz) +
            std::to_string(b.delay.shimmerAmount) + std::to_string(b.delay.mix) + ';';
        return s;
    }
    wm::mixer::Workspace current() const { return {mx.state(), rt}; }
    // A hardware output pair that has no strip yet (for example ADAT 17/18) gets one when it is assigned (Main out, Phones ...).
    int addOutputPair(int hw) {
        if (int(mx.state().buses.size()) >= int(wm::engine::kMaxBuses)) return -1;
        const std::string n = hw >= 32 ? "PH" : "ADAT " + std::to_string(hw + 1) + "/" + std::to_string(hw + 2);
        mx.addBus(n, hw, true); rt.addOutput(); if (onBusAdded) onBusAdded(n);
        eng.sync(); settingsView.rebuild();
        return int(mx.state().buses.size()) - 1;
    }
    void addChannel() {
        if (mx.state().channels.size() >= wm::engine::kMaxChannels) return;
        mx.addChannel(); rt.addInput();
        if (onChannelAdded) onChannelAdded(mx.state().channels.back().name);   // app tells PipeWire to add a port
        rt.set(int(mx.state().channels.size()) - 1, 0, 0.0f);   // to bus 1
        eng.sync();
        mixerView.refreshAll(); settingsView.rebuild(); resized();
    }
    void apply(const wm::mixer::Workspace& w) { mx.state() = w.state; rt = w.routing; last = sig(); mixerView.refreshAll(); settingsView.rebuild(); reverb.refresh(); syncFx(); fxPage.repaint(); }
    void timerCallback() override {
        eng.sync();   // hand the latest edits to the audio thread
        if (++ticks >= 1800) { ticks = 0; if (wm::mixer::settings().autosave) autosave(); }
        if (ticks % 15 == 0 && digifaceStatus) { auto t = juce::String(digifaceStatus()); if (t != dfLabel.getText()) dfLabel.setText(t, juce::dontSendNotification); }
        // Commit an undo step once the user lets go (no drag in progress) and the state differs from the last commit.
        if (juce::Desktop::getInstance().getMainMouseSource().isDragging()) return;
        if (sig() != last) { undo.push(lastWs); last = sig(); }
        lastWs = current();
        undoB.setEnabled(undo.canUndo()); redoB.setEnabled(undo.canRedo());
    }
    // App data folder: settings.json, midi.json, autosave.wmix
    static juce::File dataDir() {
        auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("WhiskerMix");
        d.createDirectory(); return d;
    }
    void loadPrefs() {
        auto f = dataDir().getChildFile("settings.json"); if (f.existsAsFile()) wm::mixer::settings().fromJson(f.loadFileAsString().toStdString());
        auto h = dataDir().getChildFile("midi.json"); if (h.existsAsFile()) hub.fromJson(h.loadFileAsString().toStdString());
    }
    void savePrefs() { dataDir().getChildFile("settings.json").replaceWithText(juce::String(wm::mixer::settings().toJson())); }
    void saveHub() { dataDir().getChildFile("midi.json").replaceWithText(juce::String(hub.toJson())); }
    void applyPrefs() {
        if (wm::mixer::settings().tooltips) { if (!tipWin) tipWin = std::make_unique<juce::TooltipWindow>(this, 1000); }
        else tipWin.reset();
        settingsView.syncSettings();
    }
    // Snapshots: named copies of the whole mixer state, kept in the app data folder (snapshots/<name>.json).
    static juce::File snapDir() { auto d = dataDir().getChildFile("snapshots"); d.createDirectory(); return d; }
    static juce::String cleanName(juce::String n) { return n.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-").trim().substring(0, 32); }
    void refreshSnaps(const juce::String& select) {
        snapBox.clear(juce::dontSendNotification); int id = 1, sel = 0;
        for (auto& n : mx.snapshotNames()) { snapBox.addItem(n, id); if (juce::String(n) == select) sel = id; ++id; }
        if (sel) snapBox.setSelectedId(sel, juce::dontSendNotification); else snapBox.setText(select, juce::dontSendNotification);
    }
    void storeSnap() {
        auto n = cleanName(snapBox.getText()); if (n.isEmpty()) n = "Snap " + juce::String(int(mx.snapshotNames().size()) + 1);
        mx.store(n.toStdString());
        snapDir().getChildFile(n + ".json").replaceWithText(juce::String(wm::mixer::toJson(mx.state())));
        refreshSnaps(n);
    }
    void recallSnap(const std::string& name) {
        auto before = current();
        if (mx.recall(name)) {
            rt.resize(int(mx.state().channels.size()), int(mx.state().buses.size()));
            undo.push(before); last = sig(); mixerView.refreshAll(); settingsView.rebuild(); reverb.refresh(); syncFx(); fxPage.repaint();
        }
    }
    void loadSnaps() {
        auto keep = mx.state();
        for (auto& f : snapDir().findChildFiles(juce::File::findFiles, false, "*.json")) {
            wm::mixer::State st; if (wm::mixer::fromJson(f.loadFileAsString().toStdString(), st)) { mx.state() = st; mx.store(f.getFileNameWithoutExtension().toStdString()); }
        }
        mx.state() = keep; refreshSnaps("");
    }
    void autosave() { dataDir().getChildFile("autosave.wmix").replaceWithText(juce::String(wm::mixer::toJson(mx.state(), rt))); }
    void loadAutosave() {
        if (!wm::mixer::settings().loadAutosaveOnStart) return;
        auto f = dataDir().getChildFile("autosave.wmix"); if (!f.existsAsFile()) return;
        wm::mixer::State s = mx.state(); wm::mixer::RoutingMatrix keep = rt;
        size_t before = mx.state().channels.size();
        if (wm::mixer::fromJson(f.loadFileAsString().toStdString(), s, rt)) {
            apply({s, rt});
            for (size_t i = before; i < s.channels.size(); ++i) if (onChannelAdded) onChannelAdded(s.channels[i].name);
            eng.sync(); midi.refresh();
        } else rt = keep;
    }
    void save() {
        chooser = std::make_unique<juce::FileChooser>("Save workspace", juce::File(), "*.wmix");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            auto f = c.getResult(); if (f != juce::File()) f.replaceWithText(wm::mixer::toJson(mx.state(), rt)); });
    }
    void load() {
        chooser = std::make_unique<juce::FileChooser>("Load workspace", juce::File(), "*.wmix");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            auto f = c.getResult(); if (!f.existsAsFile()) return;
            wm::mixer::State s = mx.state(); wm::mixer::RoutingMatrix keep = rt;
            if (wm::mixer::fromJson(f.loadFileAsString().toStdString(), s, rt)) { undo.push(current()); apply({s, rt}); if (onWorkspaceLoaded) onWorkspaceLoaded(); } });
    }
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; MockMixerView mixerView; MockRoutingView routing; ReverbView reverb; wm::engine::AudioEngine& eng;
    wm::mixer::MidiHub hub; MidiView midi; PulseView pulse; PulseHost pulseHost{pulse, pulseVp}; juce::Viewport pulseVp; SettingsView settingsView; std::unique_ptr<juce::TooltipWindow> tipWin; int ticks = 0;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    juce::TextButton undoB, redoB, addB, storeB, recallB, saveB, loadB; juce::ComboBox snapBox; juce::Label dfLabel;
    bool chromeless = false; int slot = -1; juce::String wsName;
    wm::mixer::WorkspaceUndo undo; wm::mixer::Workspace lastWs; std::string last;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
