// WhiskerMix app skeleton (JUCE). NOT built yet: needs JUCE and PipeWire. Wires the tested core together.
// UI layer will load the mockup visuals later; this file only proves the plumbing.
#include <juce_gui_extra/juce_gui_extra.h>
#include <thread>
#include "../engine/AudioEngine.h"
#include "../pipewire/PipeWireMixer.h"
#include "../mixer/StateJson.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow() : DocumentWindow("WhiskerMix", juce::Colour(0xff1b1b1f), allButtons) {
        setUsingNativeTitleBar(true);
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
        if (pw.start(*engine, ins, buses)) audio = std::thread([this] { pw.run(); });
        window = std::make_unique<MainWindow>();
    }
    void shutdown() override { pw.quit(); if (audio.joinable()) audio.join(); window.reset(); }
private:
    std::unique_ptr<wm::mixer::Mixer> mixer;
    std::unique_ptr<wm::mixer::RoutingMatrix> routing;
    std::unique_ptr<wm::engine::AudioEngine> engine;
    wm::pw::PipeWireMixer pw;
    std::thread audio;
    std::unique_ptr<MainWindow> window;
};
START_JUCE_APPLICATION(WhiskerMixApp)
