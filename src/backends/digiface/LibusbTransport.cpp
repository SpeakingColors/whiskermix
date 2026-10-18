#include "LibusbTransport.h"
#include <libusb-1.0/libusb.h>

namespace wm::digiface {
namespace {
constexpr uint16_t kVendor = 0x2A39, kProduct = 0x3FA0;
constexpr uint8_t kBulkOutEp = 0x0B;
constexpr uint8_t kRequestTypeVendorOut = 0x40;
constexpr uint8_t kRequestMixer = 0x15;
constexpr unsigned kTimeoutMs = 500;
}  // namespace

LibusbTransport::~LibusbTransport() { close(); }

bool LibusbTransport::open(std::string* error) {
    close();
    auto fail = [&](const std::string& m) {
        lastError_ = m;
        if (error) *error = m;
        close();
        return false;
    };
    if (libusb_init(&ctx_) != 0) return fail("libusb init failed");
    handle_ = libusb_open_device_with_vid_pid(ctx_, kVendor, kProduct);
    if (!handle_) return fail("Digiface USB not found (or no permission: check the udev rule)");

    // Find the interface that owns the bulk OUT endpoint.
    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(libusb_get_device(handle_), &cfg) == 0) {
        for (int i = 0; i < cfg->bNumInterfaces && bulkInterface_ < 0; ++i)
            for (int a = 0; a < cfg->interface[i].num_altsetting && bulkInterface_ < 0; ++a) {
                const auto& alt = cfg->interface[i].altsetting[a];
                for (int e = 0; e < alt.bNumEndpoints; ++e)
                    if (alt.endpoint[e].bEndpointAddress == kBulkOutEp) bulkInterface_ = alt.bInterfaceNumber;
            }
        libusb_free_config_descriptor(cfg);
    }
    if (bulkInterface_ >= 0) {
        const int rc = libusb_claim_interface(handle_, bulkInterface_);
        bulkClaimed_ = (rc == 0);
        if (!bulkClaimed_)
            lastError_ = std::string("bulk interface not claimed: ") + libusb_error_name(rc) +
                         " (control writes still work: main volume, dim, speaker switch)";
    } else {
        lastError_ = "bulk endpoint 0x0B not found in the active configuration";
    }
    return true;  // control writes work without claiming anything
}

void LibusbTransport::close() {
    if (handle_) {
        if (bulkClaimed_) libusb_release_interface(handle_, bulkInterface_);
        libusb_close(handle_);
    }
    if (ctx_) libusb_exit(ctx_);
    handle_ = nullptr;
    ctx_ = nullptr;
    bulkInterface_ = -1;
    bulkClaimed_ = false;
}

bool LibusbTransport::bulkOut(const std::vector<uint8_t>& bytes) {
    if (!handle_ || !bulkClaimed_) return false;
    int sent = 0;
    std::vector<uint8_t> copy = bytes;
    const int rc = libusb_bulk_transfer(handle_, kBulkOutEp, copy.data(), int(copy.size()), &sent, kTimeoutMs);
    if (rc != 0) lastError_ = std::string("bulk write failed: ") + libusb_error_name(rc);
    return rc == 0 && sent == int(copy.size());
}

bool LibusbTransport::controlOut(const ControlWrite& w) {
    if (!handle_) return false;
    const int rc = libusb_control_transfer(handle_, kRequestTypeVendorOut, kRequestMixer, w.value, w.index,
                                           nullptr, 0, kTimeoutMs);
    if (rc < 0) lastError_ = std::string("control write failed: ") + libusb_error_name(rc);
    return rc >= 0;
}

}  // namespace wm::digiface
