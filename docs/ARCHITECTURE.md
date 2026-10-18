# Architecture and backend plan

## Layers
1. **UI**: JUCE + cat-ui. Draws through `catui::RenderGate` so meters stop when the window is hidden.
2. **Mixer model**: channels, buses, routing matrix, snapshots. Pure C++, no hardware calls.
3. **Backends** (pluggable):
   - Digiface USB (RME): audio and clock are handled by the Linux kernel. Matrix mixer, output gain and meters need a vendor protocol that is **not yet known**.
   - MIDI (ALSA/JACK/PipeWire): hub, filters, Pulse 16 SysEx.
   - Control surfaces: Mackie Control and OSC.
4. **FX**: own DSP block chain applied in software (see the shared cat-fx plan).

## Hardware backends
- **Digiface mixer** (`src/backends/digiface/`): a pure C++ codec (gain values, channel and main-output packets) behind a small `Transport` interface, plus a recording transport for tests and dry-run. A libusb transport is the next step and is not yet tested on real hardware.
- Controls the hardware does not take over USB (mute, solo, width and similar) are done in software through PipeWire/JACK, so the UI behaves the same either way.
- The Pulse 16 uses SysEx over DIN MIDI.

Run the codec test with `g++ -std=c++17 tests/digiface_codec_test.cpp -o /tmp/dft && /tmp/dft`.

## Fallback if the matrix protocol cannot be learned
Run the mixer in software: route PipeWire/JACK ports through our own mixer and use the Digiface only as an audio interface.
