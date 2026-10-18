// Top-level view: toolbar (undo, redo, snapshots, save, load) + tabs (Mixer, EQ/Comp, Routing).
// Undo is automatic: when the state changes and no mouse drag is in progress, the previous state is pushed.
#pragma once
#include <fstream>
#include <sstream>
#include "MixerView.h"
#include "FxView.h"
#include "RoutingView.h"
#include "../mixer/UndoStack.h"
#include "../mixer/StateJson.h"

namespace wmui {

class AppView : public juce::Component, private juce::Timer {
public:
    AppView(wm::mixer::Mixer& m, wm::mixer::RoutingMatrix& r, wm::engine::AudioEngine& e)
        : mx(m), rt(r), mixerView(m, r, e), routing(m, r), eng(e) {
        tabs.addTab("Mixer", col::panel, &mixerView, false);
        tabs.addTab("EQ / Comp", col::panel, &fxPage, false);
        tabs.addTab("Routing", col::panel, &routing, false);
        addAndMakeVisible(tabs);
        auto btn = [this](juce::TextButton& b, const char* t, std::function<void()> f) { b.setButtonText(t); b.onClick = std::move(f); addAndMakeVisible(b); };
        btn(undoB, "Undo", [this] { if (undo.canUndo()) apply(undo.undo(mx.state())); });
        btn(redoB, "Redo", [this] { if (undo.canRedo()) apply(undo.redo(mx.state())); });
        btn(storeB, "Store A", [this] { mx.store("A"); });
        btn(recallB, "Recall A", [this] { if (mx.recall("A")) { last = sig(); mixerView.refreshAll(); } });
        btn(saveB, "Save", [this] { save(); });
        btn(loadB, "Load", [this] { load(); });
        fxPage.eq.params = fxPage.comp.params = [this] { return &mx.state().channels[size_t(mixerView.selectedChannel)].fx; };
        fxPage.comp.grDb = [this] { return eng.gainReductionDb(mixerView.selectedChannel); };
        for (auto* b : {&fxPage.eqOn, &fxPage.compOn}) { b->setClickingTogglesState(true); addAndMakeVisible(b); fxPage.addAndMakeVisible(b); }
        fxPage.eqOn.setButtonText("EQ"); fxPage.compOn.setButtonText("COMP");
        fxPage.eqOn.onClick = [this] { fx().eqOn = fxPage.eqOn.getToggleState(); fxPage.repaint(); };
        fxPage.compOn.onClick = [this] { fx().compOn = fxPage.compOn.getToggleState(); fxPage.repaint(); };
        mixerView.onSelect = [this](int) { syncFx(); fxPage.repaint(); };
        last = sig();
        startTimerHz(30);
    }
    void paint(juce::Graphics& g) override { g.fillAll(col::bg); }
    void resized() override {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop(34).reduced(4);
        for (auto* b : {&undoB, &redoB, &storeB, &recallB, &saveB, &loadB}) b->setBounds(bar.removeFromLeft(80).reduced(2));
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
        std::string s = wm::mixer::toJson(mx.state());
        for (auto& c : mx.state().channels) s += fxSig(c.fx) + ';';
        return s;
    }
    void apply(const wm::mixer::State& s) { mx.state() = s; last = sig(); mixerView.refreshAll(); syncFx(); fxPage.repaint(); }
    void timerCallback() override {
        eng.sync();   // hand the latest edits to the audio thread
        // Commit an undo step once the user lets go (no drag in progress) and the state differs from the last commit.
        if (juce::Desktop::getInstance().getMainMouseSource().isDragging()) return;
        if (sig() != last) { undo.push(lastState); last = sig(); }
        lastState = mx.state();
        undoB.setEnabled(undo.canUndo()); redoB.setEnabled(undo.canRedo());
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
            wm::mixer::State s = mx.state();
            if (wm::mixer::fromJson(f.loadFileAsString().toStdString(), s, rt)) { undo.push(mx.state()); apply(s); } });
    }
    wm::mixer::Mixer& mx; wm::mixer::RoutingMatrix& rt; MixerView mixerView; RoutingView routing; wm::engine::AudioEngine& eng;
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    juce::TextButton undoB, redoB, storeB, recallB, saveB, loadB;
    wm::mixer::UndoStack undo; wm::mixer::State lastState; std::string last;
    std::unique_ptr<juce::FileChooser> chooser;
};
}
