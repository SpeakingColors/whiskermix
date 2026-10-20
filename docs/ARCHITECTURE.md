# Architecture and backend plan

## Layers
1. **UI**: JUCE + cat-ui. Draws through `catui::RenderGate` so meters stop when the window is hidden.
2. **Mixer model** (`src/mixer/`): channels, buses, mute/solo, routing matrix, snapshots, undo/redo and JSON workspace files. Pure C++, no hardware calls. Run all tests with `sh tests/run.sh`.
3. **Backends** (pluggable):
   - Digiface USB (RME): audio and clock are handled by the Linux kernel. Matrix mixer, output gain and meters need a vendor protocol that is **not yet known**.
   - MIDI (ALSA/JACK/PipeWire): hub, filters, Pulse 16 SysEx.
   - Control surfaces: Mackie Control and OSC.
4. **FX**: own DSP block chain applied in software (see the shared cat-fx plan).

## Hardware backends
- **Digiface mixer** (`src/backends/digiface/`): a pure C++ codec (gain values, channel and main-output packets) behind a small `Transport` interface, plus a recording transport for tests and dry-run. `LibusbTransport` sends the writes over USB; it does not detach the kernel audio driver, so audio keeps running. It compiles but is not yet tested on real hardware. Install `packaging/udev/70-whiskermix-digiface.rules` so no root is needed.
- Controls the hardware does not take over USB (mute, solo, width and similar) are done in software through PipeWire/JACK, so the UI behaves the same either way.
- **Names** (`src/names/`): one list of channel names generates a WirePlumber rule so the device and its channels carry the same names in system sound settings, patchbays and DAWs. Experimental: run `tools/wm-diagnose.sh` to check the real node names and how each app shows them.
- **Pulse 16** (`src/backends/pulse16/`): a pure C++ SysEx codec and a session model (connect handshake, live state, setters) behind a small `Transport` interface. The protocol was reverse engineered from RemoteFish captures: see `docs/PULSE16-PROTOCOL.md`. Tests: `tests/pulse16_test.cpp`. `src/app/PulseLink.h` (MIDI ports) and `src/app/PulseView.h` (the page) sit on top; the page is built from the same session, never from the screen of the unit.

Tests: `g++ -std=c++17 tests/digiface_codec_test.cpp -o /tmp/t1 && /tmp/t1` and the same for `tests/names_test.cpp`.

## Fallback if the matrix protocol cannot be learned
Run the mixer in software: route PipeWire/JACK ports through our own mixer and use the Digiface only as an audio interface.

## Audio thread rules (from the JUCE best-practices guide)
- No allocation, locks, file I/O or logging in `AudioEngine::process`. All buffers are sized in `prepare()`.
- Denormals are flushed to zero per block. Gain changes are smoothed (about 5 ms).
- UI edits reach the audio thread through a lock-free triple buffer (`AudioEngine::sync`); meters come back through atomics. Tested with a two-thread race.
