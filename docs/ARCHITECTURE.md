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
The Digiface matrix mixer, output gain and meters are not exposed by the Linux kernel driver. The vendor protocol is still being researched. The Pulse 16 uses SysEx over DIN MIDI.

## Fallback if the matrix protocol cannot be learned
Run the mixer in software: route PipeWire/JACK ports through our own mixer and use the Digiface only as an audio interface.
