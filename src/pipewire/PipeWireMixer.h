#pragma once
#include <string>
#include <vector>
#include "../engine/AudioEngine.h"
namespace wm::pw {
class PipeWireMixer {
public:
    struct Impl;
    PipeWireMixer();
    ~PipeWireMixer();
    bool start(engine::AudioEngine&, const std::vector<std::string>& inputNames,
               const std::vector<std::string>& busNames, const std::string& nodeName = "WhiskerMix");
    void addInput(const std::string& name);   // live: new input port appears in PipeWire
    void addBus(const std::string& name);
    void run();
    void quit();
    void stop();
private:
    Impl* d_;
};
}
