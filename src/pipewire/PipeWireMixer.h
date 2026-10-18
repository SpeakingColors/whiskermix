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
    void run();
    void quit();
    void stop();
private:
    Impl* d_;
};
}
