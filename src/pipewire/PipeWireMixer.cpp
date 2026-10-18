// PipeWire adapter: exposes the engine as ONE filter node with mono inputs and stereo bus outputs.
// Written against libpipewire-0.3 pw_filter API. NOT compiled or run yet (no PipeWire headers in the dev sandbox).
// First build and test on Bazzite: see docs/FIRST_RUN.md.
#include <pipewire/pipewire.h>
#include <pipewire/filter.h>
#include <spa/param/audio/format-utils.h>
#include <atomic>
#include <string>
#include <vector>
#include "PipeWireMixer.h"

namespace wm::pw {

struct Port { void* data; };

struct PipeWireMixer::Impl {
    engine::AudioEngine* eng = nullptr;
    pw_main_loop* loop = nullptr;
    pw_filter* filter = nullptr;
    // Fixed capacity: the process callback reads these while the main loop may add ports.
    Port* in[engine::kMaxChannels] = {};
    Port* out[engine::kMaxBuses * 2] = {};
    const float* inBuf[engine::kMaxChannels] = {};
    float* outBuf[engine::kMaxBuses * 2] = {};
    std::atomic<int> nIn{0}, nOut{0};
    std::string pendingName;
    bool pendingIsBus = false;
};

static constexpr int kMaxQuantum = 8192;
static float gSilence[kMaxQuantum] = {};
static float gSink[kMaxQuantum];

static void onProcess(void* ud, spa_io_position* pos) {
    auto* s = static_cast<PipeWireMixer::Impl*>(ud);
    uint32_t n = pos->clock.duration;
    if (n > uint32_t(kMaxQuantum)) return;
    int ni = s->nIn.load(std::memory_order_acquire), no = s->nOut.load(std::memory_order_acquire);
    for (int i = 0; i < ni; ++i) {
        auto* b = static_cast<float*>(pw_filter_get_dsp_buffer(s->in[i], n));
        s->inBuf[i] = b ? b : gSilence;                       // unlinked input = silence
    }
    for (int i = ni; i < int(engine::kMaxChannels); ++i) s->inBuf[i] = gSilence;
    for (int o = 0; o < no; ++o) {
        auto* b = static_cast<float*>(pw_filter_get_dsp_buffer(s->out[o], n));
        s->outBuf[o] = b ? b : gSink;
    }
    for (int o = no; o < int(engine::kMaxBuses) * 2; ++o) s->outBuf[o] = gSink;
    s->eng->process(s->inBuf, s->outBuf, int(n));
}

static const pw_filter_events kEvents = []{
    pw_filter_events e{}; e.version = PW_VERSION_FILTER_EVENTS; e.process = onProcess; return e; }();

static Port* addPort(PipeWireMixer::Impl* d, enum spa_direction dir, const std::string& name, const char* chan) {
    return static_cast<Port*>(pw_filter_add_port(d->filter, dir, PW_FILTER_PORT_FLAG_MAP_BUFFERS, sizeof(Port),
        pw_properties_new(PW_KEY_FORMAT_DSP, "32 bit float mono audio", PW_KEY_PORT_NAME, name.c_str(),
                          PW_KEY_AUDIO_CHANNEL, chan, nullptr), nullptr, 0));
}
static void addInputNow(PipeWireMixer::Impl* d, const std::string& name) {
    int i = d->nIn.load();
    if (i >= int(engine::kMaxChannels)) return;
    d->in[i] = addPort(d, PW_DIRECTION_INPUT, name, "UNKNOWN");
    d->nIn.store(i + 1, std::memory_order_release);
}
static void addBusNow(PipeWireMixer::Impl* d, const std::string& name) {
    int o = d->nOut.load();
    if (o + 2 > int(engine::kMaxBuses) * 2) return;
    d->out[o] = addPort(d, PW_DIRECTION_OUTPUT, name + " L", "FL");
    d->out[o + 1] = addPort(d, PW_DIRECTION_OUTPUT, name + " R", "FR");
    d->nOut.store(o + 2, std::memory_order_release);
}

PipeWireMixer::PipeWireMixer() : d_(new Impl) {}
PipeWireMixer::~PipeWireMixer() { stop(); delete d_; }

bool PipeWireMixer::start(engine::AudioEngine& eng, const std::vector<std::string>& inNames,
                          const std::vector<std::string>& busNames, const std::string& nodeName) {
    d_->eng = &eng;
    eng.prepare(kMaxQuantum);   // allocate here, never in the process callback
    pw_init(nullptr, nullptr);
    d_->loop = pw_main_loop_new(nullptr);
    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Filter", PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_NODE_NAME, "whiskermix", PW_KEY_NODE_DESCRIPTION, nodeName.c_str(), nullptr);
    d_->filter = pw_filter_new_simple(pw_main_loop_get_loop(d_->loop), "whiskermix", props, &kEvents, d_);
    if (!d_->filter) return false;
    for (auto& n : inNames) addInputNow(d_, n);
    for (auto& n : busNames) addBusNow(d_, n);
    return pw_filter_connect(d_->filter, PW_FILTER_FLAG_RT_PROCESS, nullptr, 0) >= 0;
}

// Add a port while running. Must run on the PipeWire main loop thread, so it is queued with pw_loop_invoke.
static int invokeAdd(spa_loop*, bool, uint32_t, const void*, size_t, void* ud) {
    auto* d = static_cast<PipeWireMixer::Impl*>(ud);
    d->pendingIsBus ? addBusNow(d, d->pendingName) : addInputNow(d, d->pendingName);
    return 0;
}
void PipeWireMixer::addInput(const std::string& name) {
    if (!d_->loop) return;
    d_->pendingName = name; d_->pendingIsBus = false;
    pw_loop_invoke(pw_main_loop_get_loop(d_->loop), invokeAdd, 0, nullptr, 0, true, d_);
}
void PipeWireMixer::addBus(const std::string& name) {
    if (!d_->loop) return;
    d_->pendingName = name; d_->pendingIsBus = true;
    pw_loop_invoke(pw_main_loop_get_loop(d_->loop), invokeAdd, 0, nullptr, 0, true, d_);
}

void PipeWireMixer::run() { pw_main_loop_run(d_->loop); }          // blocks; call from a worker thread
void PipeWireMixer::quit() { if (d_->loop) pw_main_loop_quit(d_->loop); }
void PipeWireMixer::stop() {
    if (d_->filter) { pw_filter_destroy(d_->filter); d_->filter = nullptr; }
    if (d_->loop) { pw_main_loop_destroy(d_->loop); d_->loop = nullptr; }
}

}  // namespace wm::pw
