// Windows only: captures what Windows plays to an output device (WASAPI loopback) and delivers it to the
// virtual feed as computer audio (virtual channels 0 and 1 = the COMPUTER strip). No driver needed.
// Limit: it hears everything played to that device, not one program. Pick a device WhiskerMix itself does not
// play to, or its own output is captured again.
#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <audioclient.h>
#include <atomic>
#include <string>
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#include "../engine/VirtualFeed.h"

class WasapiLoopback {
public:
    explicit WasapiLoopback(wm::engine::VirtualFeed& f) : feed(f) {}
    ~WasapiLoopback() { stop(); }

    // Friendly names of the output devices, in the order start() counts them.
    static std::vector<std::string> deviceNames() {
        std::vector<std::string> names;
        const bool inited = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        IMMDeviceEnumerator* en = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) && en) {
            IMMDeviceCollection* col = nullptr;
            if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
                UINT n = 0; col->GetCount(&n);
                for (UINT i = 0; i < n; ++i) {
                    IMMDevice* d = nullptr; std::string nm = "Output " + std::to_string(i + 1);
                    if (SUCCEEDED(col->Item(i, &d)) && d) {
                        IPropertyStore* ps = nullptr;
                        if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &ps)) && ps) {
                            PROPVARIANT v; PropVariantInit(&v);
                            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) {
                                int len = WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                                if (len > 1) { nm.assign(size_t(len - 1), '\0'); WideCharToMultiByte(CP_UTF8, 0, v.pwszVal, -1, &nm[0], len, nullptr, nullptr); }
                            }
                            PropVariantClear(&v); ps->Release();
                        }
                        d->Release();
                    }
                    names.push_back(nm);
                }
                col->Release();
            }
            en->Release();
        }
        if (inited) CoUninitialize();
        return names;
    }

    // index -1 = the Windows default output. Safe to call again to switch device.
    void start(int index) {
        stop(); running = true; setState("starting");
        th = std::thread([this, index] { run(index); });
    }
    void stop() { running = false; if (th.joinable()) th.join(); }
    void setTargetRate(double r) { target = r; }
    std::string status() { std::lock_guard<std::mutex> g(mu); return state; }

private:
    void setState(const std::string& s) { std::lock_guard<std::mutex> g(mu); state = s; }
    void run(int index) {
        const bool inited = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioClient* ac = nullptr; IAudioCaptureClient* cc = nullptr; WAVEFORMATEX* fmt = nullptr;
        auto cleanup = [&] {
            if (ac) ac->Stop();
            if (cc) cc->Release();
            if (fmt) CoTaskMemFree(fmt);
            if (ac) ac->Release();
            if (dev) dev->Release();
            if (en) en->Release();
            if (inited) CoUninitialize();
        };
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) || !en) { setState("loopback: no device enumerator"); cleanup(); return; }
        HRESULT hr;
        if (index < 0) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        else {
            IMMDeviceCollection* col = nullptr; hr = en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col);
            if (SUCCEEDED(hr) && col) { hr = col->Item(UINT(index), &dev); col->Release(); }
        }
        if (FAILED(hr) || !dev) { setState("loopback: output device not found"); cleanup(); return; }
        if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac)) || !ac) { setState("loopback: cannot open the device"); cleanup(); return; }
        if (FAILED(ac->GetMixFormat(&fmt)) || !fmt) { setState("loopback: no mix format"); cleanup(); return; }
        if (FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, fmt, nullptr))) { setState("loopback: device refused capture"); cleanup(); return; }
        if (FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cc)) || !cc) { setState("loopback: no capture client"); cleanup(); return; }
        if (FAILED(ac->Start())) { setState("loopback: cannot start"); cleanup(); return; }

        const int ch = fmt->nChannels; const double srcRate = double(fmt->nSamplesPerSec);
        bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT; int bits = fmt->wBitsPerSample;
        if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE && fmt->cbSize >= 22) {
            const auto* ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(fmt);
            isFloat = ex->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT;
        }
        setState("loopback on: " + std::to_string(int(srcRate)) + " Hz, " + std::to_string(ch) + " ch");
        std::vector<float> l, r, ol, orr; double pos = 0.0; float lastL = 0, lastR = 0;
        while (running) {
            Sleep(4);
            UINT32 pk = 0;
            while (running && SUCCEEDED(cc->GetNextPacketSize(&pk)) && pk > 0) {
                BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
                l.resize(frames); r.resize(frames);
                for (UINT32 i = 0; i < frames; ++i) {
                    float a = 0, b = 0;
                    if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                        if (isFloat && bits == 32) { const float* f = reinterpret_cast<const float*>(data) + size_t(i) * size_t(ch); a = f[0]; b = ch > 1 ? f[1] : f[0]; }
                        else if (bits == 16) { const short* f = reinterpret_cast<const short*>(data) + size_t(i) * size_t(ch); a = f[0] / 32768.0f; b = (ch > 1 ? f[1] : f[0]) / 32768.0f; }
                        else if (bits == 32) { const int* f = reinterpret_cast<const int*>(data) + size_t(i) * size_t(ch); a = float(f[0] / 2147483648.0); b = float((ch > 1 ? f[1] : f[0]) / 2147483648.0); }
                    }
                    l[i] = a; r[i] = b;
                }
                cc->ReleaseBuffer(frames);
                // linear resample to the engine rate
                const double dst = target.load() > 1000.0 ? target.load() : srcRate, step = srcRate / dst;
                ol.clear(); orr.clear();
                while (pos < double(frames)) {
                    const int i0 = int(std::floor(pos)); const double fr = pos - double(i0);
                    const float a0 = i0 == 0 ? lastL : l[size_t(i0 - 1)], b0 = i0 == 0 ? lastR : r[size_t(i0 - 1)];
                    const float a1 = l[size_t(i0)], b1 = r[size_t(i0)];
                    ol.push_back(float(a0 + (a1 - a0) * fr)); orr.push_back(float(b0 + (b1 - b0) * fr));
                    pos += step;
                }
                pos -= double(frames);
                if (frames) { lastL = l[frames - 1]; lastR = r[frames - 1]; }
                if (!ol.empty()) { feed.write(0, ol.data(), int(ol.size())); feed.write(1, orr.data(), int(orr.size())); }
            }
        }
        cleanup();
    }
    wm::engine::VirtualFeed& feed;
    std::thread th; std::atomic<bool> running{false}; std::atomic<double> target{0.0};
    std::mutex mu; std::string state = "loopback off";
};
#endif
