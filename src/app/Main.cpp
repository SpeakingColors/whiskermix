// WhiskerMix app skeleton (JUCE). NOT built yet: needs JUCE and PipeWire. Wires the tested core together.
// UI layer will load the mockup visuals later; this file only proves the plumbing.
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>
#include "../engine/AudioEngine.h"
#ifdef WM_JUCE_AUDIO
#include "JuceAudio.h"
#else
#include "../pipewire/PipeWireMixer.h"
#endif
#include "../mixer/StateJson.h"
#include "../mixer/Layout.h"
#include "AppView.h"
#include "MockFrame.h"
#include "DigifaceLink.h"

class MainWindow : public juce::DocumentWindow {
public:
    // content = the app view; the window shows it inside the mockup frame (wood cheeks, logo, tabs), scaled with the aspect kept.
    MainWindow(wmui::AppView* app) : DocumentWindow("WhiskerMix", wmui::mock::outer, allButtons) {
        setUsingNativeTitleBar(true);
        auto* host = new wmui::ScaledHost(*app);
        host->frame.resetSize = [this] { setContentComponentSize(wmui::mock::kW, wmui::mock::kH); };
        owned.reset(app); setContentOwned(host, true);
        setResizable(true, true);
        constrainer.setFixedAspectRatio(double(wmui::mock::kW) / wmui::mock::kH); constrainer.setMinimumSize(540, 466);
        setConstrainer(&constrainer);
        setContentComponentSize(wmui::mock::kW, wmui::mock::kH);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }
    ~MainWindow() override { clearContentComponent(); }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
private:
    juce::ComponentBoundsConstrainer constrainer; std::unique_ptr<wmui::AppView> owned;
};

class WhiskerMixApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "WhiskerMix"; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    void initialise(const juce::String&) override {
        juce::LookAndFeel::setDefaultLookAndFeel(&look);
        // Starts from the studio layout (hardware inputs, software playback, hardware outputs, control room).
        auto built = wm::mixer::buildFromLayout(wm::mixer::studioLayout());
        mixer = std::make_unique<wm::mixer::Mixer>(built.mixer);
        routing = std::make_unique<wm::mixer::RoutingMatrix>(built.routing);
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
        // Digiface hardware mixer: bus 0 = Main (control room), bus 1 = Speaker B (iLoud Micro).
        digiface = std::make_unique<wmui::DigifaceLink>(*mixer, 0, (mixer->state().buses.size() >= 2 && mixer->state().buses[1].controlRoom) ? 1 : -1);
        view->digifaceStatus = [this] { return digiface->status(); };
#ifdef WM_JUCE_AUDIO
        view->audioDevices = [this] {
            std::vector<wmui::MockRoutingView::Dev> v;
            if (auto* t = adm.getCurrentDeviceTypeObject()) {
                auto outs = t->getDeviceNames(false), ins = t->getDeviceNames(true);
                juce::StringArray all; all.addArray(outs); for (auto& n : ins) all.addIfNotAlreadyThere(n);
                for (auto& n : all) v.push_back({n.toStdString(), std::string(ins.contains(n) ? "in" : "") + (ins.contains(n) && outs.contains(n) ? " + " : "") + (outs.contains(n) ? "out" : ""), "", true});
                if (!v.empty()) v[0].status = "clock master";
            }
            return v;
        };
#endif
        view->onWorkspaceLoaded = [this] { digiface->resend(); };
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
        digiface.reset(); window.reset(); juce::LookAndFeel::setDefaultLookAndFeel(nullptr); }
private:
    wmui::SodaLook look;
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
    std::unique_ptr<wmui::DigifaceLink> digiface;
};
START_JUCE_APPLICATION(WhiskerMixApp)
