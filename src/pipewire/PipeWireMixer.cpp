// PipeWire adapter: exposes the engine as ONE filter node with N mono inputs and 2*B outputs.
// Written against libpipewire-0.3 pw_filter API. NOT compiled or run yet (no PipeWire headers in the dev sandbox).
// First build and test on Bazzite: see docs/FIRST_RUN.md.
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/param/audio/format-utils.h>
#include <string>
#include <vector>
#include "PipeWireMixer.h"

namespace wm::pw {

struct Port { void* data; };

struct PipeWireMixer::Impl {
    engine::AudioEngine* eng = nullptr;
    pw_main_loop* loop = nullptr;
    pw_filter* filter = nullptr;
    std::vector<Port*> in, out;
    std::vector<const float*> inBuf;
    std::vector<float*> outBuf;
    uint32_t maxFrames = 1024;
};

static void onProcess(void* ud, spa_io_position* pos) {
    auto* s = static_cast<PipeWireMixer::Impl*>(ud);
    uint32_t n = pos->clock.duration;
    for (size_t i = 0; i < s->in.size(); ++i) {
        auto* b = static_cast<float*>(pw_filter_get_dsp_buffer(s->in[i], n));
        s->inBuf[i] = b;
    }
    for (size_t o = 0; o < s->out.size(); ++o) {
        auto* b = static_cast<float*>(pw_filter_get_dsp_buffer(s->out[o], n));
        s->outBuf[o] = b;
    }
    for (size_t i = 0; i < s->in.size(); ++i) if (!s->inBuf[i]) return;   // not linked yet
    for (size_t o = 0; o < s->out.size(); ++o) if (!s->outBuf[o]) return;
    s->eng->process(s->inBuf.data(), s->outBuf.data(), int(n));
}

static const pw_filter_events kEvents = []{
    pw_filter_events e{}; e.version = PW_VERSION_FILTER_EVENTS; e.process = onProcess; return e; }();

PipeWireMixer::PipeWireMixer() : d_(new Impl) {}
PipeWireMixer::~PipeWireMixer() { stop(); delete d_; }

bool PipeWireMixer::start(engine::AudioEngine& eng, const std::vector<std::string>& inNames,
                          const std::vector<std::string>& busNames, const std::string& nodeName) {
    d_->eng = &eng;
    pw_init(nullptr, nullptr);
    d_->loop = pw_main_loop_new(nullptr);
    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Filter", PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_NODE_NAME, "whiskermix", PW_KEY_NODE_DESCRIPTION, nodeName.c_str(), nullptr);
    d_->filter = pw_filter_new_simple(pw_main_loop_get_loop(d_->loop), "whiskermix", props, &kEvents, d_);
    if (!d_->filter) return false;
    auto addPort = [&](enum spa_direction dir, const std::string& name, const std::string& chan) {
        return pw_filter_add_port(d_->filter, dir, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(Port),
            pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, name.c_str(),
                              PW_KEY_AUDIO_CHANNEL, chan.c_str(), nullptr), nullptr, 0);
    };
    for (auto& n : inNames) d_->in.push_back(static_cast<Port*>(addPort(PW_DIRECTION_INPUT, n, "UNKNOWN")));
    for (auto& n : busNames) {
        d_->out.push_back(static_cast<Port*>(addPort(PW_DIRECTION_OUTPUT, n + " L", "FL")));
        d_->out.push_back(static_cast<Port*>(addPort(PW_DIRECTION_OUTPUT, n + " R", "FR")));
    }
    d_->inBuf.assign(d_->in.size(), nullptr);
    d_->outBuf.assign(d_->out.size(), nullptr);
    if (pw_filter_connect(d_->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) < 0) return false;
    return true;
}

void PipeWireMixer::run() { pw_main_loop_run(d_->loop); }          // blocks; call from a worker thread
void PipeWireMixer::quit() { if (d_->loop) pw_main_loop_quit(d_->loop); }
void PipeWireMixer::stop() {
    if (d_->filter) { pw_filter_destroy(d_->filter); d_->filter = nullptr; }
    if (d_->loop) { pw_main_loop_destroy(d_->loop); d_->loop = nullptr; }
}

}  // namespace wm::pw
