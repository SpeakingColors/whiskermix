# Architecture and backend plan

## Layers
1. **UI**: JUCE + cat-ui. Draws through `catui::RenderGate` so meters stop when the window is hidden.
2. **Mixer model**: channels, buses, routing matrix, snapshots. Pure C++, no hardware calls.
3. **Backends** (pluggable):
   - Digiface USB (RME): audio and clock are handled by the Linux kernel. Matrix mixer, output gain and meters need a vendor protocol that is **not yet known**.
   - MIDI (ALSA/JACK/PipeWire): hub, filters, Pulse 16 SysEx.
   - Control surfaces: Mackie Control and OSC.
4. **FX**: own DSP block chain applied in software (see the shared cat-fx plan).

## Digiface USB status
Captured so far (USBPcap): enumeration plus a poll of four vendor control IN requests (0xC0): 0x1C, 0x1E, 0x1F (4 bytes each) and 0x11 (16 bytes). No vendor OUT writes yet, so the mixer protocol is unknown. A capture with TotalMix FX in use, one action per file, is needed. Details: project note `digiface-usb-notes`.

## Pulse 16 status
SysEx replies were truncated in earlier captures by a clone cable (fc02:0101). A proper MIDI interface is needed to get the full replies.

## Fallback if the matrix protocol cannot be learned
Run the mixer in software: route PipeWire/JACK ports through our own mixer and use the Digiface only as an audio interface.
