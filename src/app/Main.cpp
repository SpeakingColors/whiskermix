// WhiskerMix app skeleton (JUCE). NOT built yet: needs JUCE and PipeWire. Wires the tested core together.
// UI layer will load the mockup visuals later; this file only proves the plumbing.
#include "BinaryData.h"
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
        setIcon(juce::ImageFileFormat::loadFrom(BinaryData::whiskermixicon256_png, BinaryData::whiskermixicon256_pngSize));
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
        adm.initialiseWithDefaultDevices(0, 4); // 0 inputs: never open the microphone by default
        adm.addAudioCallback(jaudio.get());
#else
        if (pw.start(*engine, ins, buses)) audio = std::thread([this] { pw.run(); });
#endif
        auto* view = new wmui::AppView(*mixer, *routing, *engine);
        // Digiface hardware mixer: bus 0 = Main (control room), bus 1 = Speaker B (iLoud Micro).
        digiface = std::make_unique<wmui::DigifaceLink>(*mixer, 0, (mixer->state().buses.size() >= 2 && mixer->state().buses[1].controlRoom) ? 1 : -1);
#ifdef WM_JUCE_AUDIO
        // Windows/macOS test build: no USB control, but say what is really there (is an RME audio device listed?)
        view->digifaceStatus = [this] {
            static bool seen = false; static juce::uint32 last = 0; const auto now = juce::Time::getMillisecondCounter();
            if (last == 0 || now - last > 3000) { last = now; seen = false;
            for (auto* t : adm.getAvailableDeviceTypes()) { t->scanForDevices(); for (auto& n : t->getDeviceNames(false)) if (n.containsIgnoreCase("digiface") || n.containsIgnoreCase("madiface") || n.containsIgnoreCase("rme")) seen = true; } }
            return std::string(seen ? "Digiface USB found (audio only)" : "Digiface USB not found");
        };
#else
        view->digifaceStatus = [this] { return digiface->status(); };
#endif
#ifdef WM_JUCE_AUDIO
        // Audio interface choice (Settings > Devices): every device of every driver type, e.g. "Windows Audio: Digiface USB". Index 0 = system default, no input.
        for (auto* t : adm.getAvailableDeviceTypes()) { t->scanForDevices(); auto outs = t->getDeviceNames(false), ins = t->getDeviceNames(true);
            juce::StringArray all; all.addArray(outs); for (auto& n : ins) all.addIfNotAlreadyThere(n);
            for (auto& n : all) devs.push_back({t->getTypeName(), n}); }
        view->deviceList = [this] { juce::StringArray l; l.add("System default"); for (auto& d : devs) l.add(d.first + ": " + d.second); return l; };
        view->audioStatus = [this] {
            auto* d = adm.getCurrentAudioDevice();
            if (!d) return juce::String("no device open") + (audioErr.isNotEmpty() ? ": " + audioErr : juce::String());
            return d->getName() + ": " + juce::String(d->getActiveInputChannels().countNumberOfSetBits()) + " in / " + juce::String(d->getActiveOutputChannels().countNumberOfSetBits()) + " out, "
                + juce::String(d->getCurrentSampleRate() / 1000.0, 1) + " kHz, " + juce::String(d->getCurrentBufferSizeSamples()) + " smp" + (audioErr.isNotEmpty() ? " | " + audioErr : juce::String());
        };
        view->applyAudio = [this] {
            auto& st = wm::mixer::settings(); const int sel = int(st.getOpt("iface", 0) + 0.5f);
            static const double rates[] = {44100, 48000, 88200, 96000}; static const int bufs[] = {32, 64, 128, 256, 512, 1024, 2048};
            const double rate = rates[juce::jlimit(0, 3, int(st.getOpt("rate", 1) + 0.5f))]; const int buf = bufs[juce::jlimit(0, 6, int(st.getOpt("buf", 5) + 0.5f))];
            const int key = sel * 100000 + int(rate) / 100 * 10 + int(buf); static int lastKey = -1; if (key == lastKey) return; lastKey = key;
            if (sel <= 0 || sel > int(devs.size())) return;     // system default: leave what JUCE opened (output only)
            auto& d = devs[size_t(sel - 1)]; adm.setCurrentAudioDeviceType(d.first, true);
            juce::AudioDeviceManager::AudioDeviceSetup s; adm.getAudioDeviceSetup(s);
            s.outputDeviceName = d.second; s.inputDeviceName = d.second; s.sampleRate = rate; s.bufferSize = buf;
            s.useDefaultInputChannels = false; s.useDefaultOutputChannels = false; s.inputChannels.setRange(0, 64, true); s.outputChannels.setRange(0, 64, true);
            audioErr = adm.setAudioDeviceSetup(s, true);
            if (audioErr.isNotEmpty()) { const auto first = audioErr; s.inputDeviceName = {}; s.inputChannels.clear(); audioErr = adm.setAudioDeviceSetup(s, true); if (audioErr.isEmpty()) audioErr = "inputs refused: " + first; }   // output-only devices
        };
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
#ifndef WM_JUCE_AUDIO
        view->onBusAdded = [this](const std::string& n) { pw.addBus(n); };
#endif
        view->onWorkspaceLoaded = [this] { digiface->resend(); };
#ifndef WM_JUCE_AUDIO
        view->onChannelAdded = [this](const std::string& n) { pw.addInput(n); };
#endif
#ifdef WM_JUCE_AUDIO
        if (view->applyAudio) view->applyAudio();
#endif
        window = std::make_unique<MainWindow>(view);
        view->onAlwaysOnTop = [this](bool v) { if (window) window->setAlwaysOnTop(v); };
        if (wm::mixer::settings().getOpt("alwaysTop", 0) > 0.5f) window->setAlwaysOnTop(true);
        if (wm::mixer::settings().getOpt("noSaver", 0) > 0.5f) juce::Desktop::getInstance().setScreenSaverEnabled(false);
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
    juce::AudioDeviceManager adm; std::vector<std::pair<juce::String, juce::String>> devs; juce::String audioErr;
    std::unique_ptr<JuceAudio> jaudio;
#else
    wm::pw::PipeWireMixer pw;
    std::thread audio;
#endif
    std::unique_ptr<MainWindow> window;
    std::unique_ptr<wmui::DigifaceLink> digiface;
};
START_JUCE_APPLICATION(WhiskerMixApp)
