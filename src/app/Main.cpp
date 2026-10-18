// WhiskerMix app skeleton (JUCE). NOT built yet: needs JUCE and PipeWire. Wires the tested core together.
// UI layer will load the mockup visuals later; this file only proves the plumbing.
#include <juce_gui_extra/juce_gui_extra.h>
#include <thread>
#include "../engine/AudioEngine.h"
#ifdef WM_JUCE_AUDIO
#include "JuceAudio.h"
#else
#include "../pipewire/PipeWireMixer.h"
#endif
#include "../mixer/StateJson.h"
#include "AppView.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(juce::Component* content) : DocumentWindow("WhiskerMix", juce::Colour(0xff1b1b1f), allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
        centreWithSize(1280, 800);
        setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class WhiskerMixApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "WhiskerMix"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {
        mixer = std::make_unique<wm::mixer::Mixer>(8, 2);
        routing = std::make_unique<wm::mixer::RoutingMatrix>(8, 2);
        engine = std::make_unique<wm::engine::AudioEngine>(*mixer, *routing);
        std::vector<std::string> ins, buses;
        for (auto& c : mixer->state().channels) ins.push_back(c.name);
        for (auto& b : mixer->state().buses) buses.push_back(b.name);
#ifdef WM_JUCE_AUDIO
        jaudio = std::make_unique<JuceAudio>(*engine);
        adm.initialiseWithDefaultDevices(8, 4);
        adm.addAudioCallback(jaudio.get());
#else
        if (pw.start(*engine, ins, buses)) audio = std::thread([this] { pw.run(); });
#endif
        auto* view = new wmui::AppView(*mixer, *routing, *engine);
#ifndef WM_JUCE_AUDIO
        view->onChannelAdded = [this](const std::string& n) { pw.addInput(n); };
#endif
        window = std::make_unique<MainWindow>(view);
    }
    void shutdown() override {
#ifdef WM_JUCE_AUDIO
        adm.removeAudioCallback(jaudio.get());
#else
        pw.quit(); if (audio.joinable()) audio.join();
#endif
        window.reset(); }
private:
    std::unique_ptr<wm::mixer::Mixer> mixer;
    std::unique_ptr<wm::mixer::RoutingMatrix> routing;
    std::unique_ptr<wm::engine::AudioEngine> engine;
#ifdef WM_JUCE_AUDIO
    juce::AudioDeviceManager adm;
    std::unique_ptr<JuceAudio> jaudio;
#else
    wm::pw::PipeWireMixer pw;
    std::thread audio;
#endif
    std::unique_ptr<MainWindow> window;
};
START_JUCE_APPLICATION(WhiskerMixApp)
