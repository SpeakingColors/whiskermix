// Transport interface for the Digiface mixer backend. The codec never touches hardware.
#pragma once
#include <cstdint>
#include <vector>
#include "DigifaceCodec.h"

namespace wm::digiface {

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool bulkOut(const std::vector<uint8_t>& bytes) = 0;       // endpoint 0x0B
    virtual bool controlOut(const ControlWrite& w) = 0;                 // 0x40 / 0x15
};

// Records writes instead of sending them (tests, dry-run mode in the UI).
class RecordingTransport : public Transport {
public:
    std::vector<std::vector<uint8_t>> bulk;
    std::vector<ControlWrite> control;
    bool bulkOut(const std::vector<uint8_t>& b) override { bulk.push_back(b); return true; }
    bool controlOut(const ControlWrite& w) override { control.push_back(w); return true; }
};

}  // namespace wm::digiface
