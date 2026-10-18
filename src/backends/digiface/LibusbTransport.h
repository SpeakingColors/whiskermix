// Real USB transport for the Digiface mixer backend (libusb). Not yet tested on hardware.
#pragma once
#include <string>
#include "DigifaceTransport.h"

struct libusb_context;
struct libusb_device_handle;

namespace wm::digiface {

class LibusbTransport : public Transport {
public:
    LibusbTransport() = default;
    ~LibusbTransport() override;
    // Opens the first Digiface USB (2a39:3fa0). Does NOT detach the kernel audio driver:
    // if the bulk interface is busy, bulk writes fail with a clear message and audio keeps running.
    bool open(std::string* error = nullptr);
    void close();
    bool isOpen() const { return handle_ != nullptr; }
    bool bulkOut(const std::vector<uint8_t>& bytes) override;
    bool controlOut(const ControlWrite& w) override;
    const std::string& lastError() const { return lastError_; }

private:
    libusb_context* ctx_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    int bulkInterface_ = -1;
    bool bulkClaimed_ = false;
    std::string lastError_;
};

}  // namespace wm::digiface
