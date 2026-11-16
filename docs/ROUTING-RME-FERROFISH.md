# Routing: RME Digiface USB + Ferrofish Pulse 16 DX (and what Linux can do)

Companion page: [ROUTING.md](ROUTING.md) (clock plan, PipeWire links, `src/routing/AdatRouting.h`). This page was written from the full manuals; where the two differ, this page's table of sources wins for the channel counts. Table code here: `src/mixer/AdatLayout.h` (overlaps in part with `src/routing/AdatRouting.h`, merge later).

Status of WhiskerMix: test builds, not yet run on real hardware (see the README and ROADMAP). This page says what the makers document, what the code assumes, and what nobody has tried yet. Not affiliated with RME or Ferrofish.

Tags used below:
- **documented**: written in the maker's manual or in the Linux kernel source. Source named.
- **owner capture**: seen in the owner's own USB or MIDI captures (see DIGIFACE-PROTOCOL.md, PULSE16-PROTOCOL.md).
- **assumed**: follows from the documents but nobody checked it on the hardware.
- **untested on hardware**: code exists, never run against the real device.

## Sources
1. RME, Digiface USB User's Guide (rme-audio.de/downloads/dface_usb_e.pdf, 70 pages, product page lists version 1.3). Chapters used: 4, 7, 8.3, 9.2, 19 to 22, 25, and the FAQ "How can I use the Digiface USB under Linux?".
2. Ferrofish, PULSE16 DX Users Guide, version 2.0c (28 pages). Sections: Main Menu (Clock, SMUX modes, Routing, Preset, MIDI control), "I/Os - ADAT", "I/Os - MIDI", "Remote Software", technical specifications. (The older PULSE16 guide v1.0a was also read; it agrees.)
3. Linux kernel patch "ALSA: usb-audio: Add mixer quirk for RME Digiface USB" (Sept 2024, applied by Takashi Iwai), lkml.iu.edu/hypermail/linux/kernel/2409.0/00332.html and the series "Add basic support for RME Digiface USB".
4. Not found: any Ferrofish document listing the SysEx messages of the Pulse 16 series. The manual only says the MIDI interface must pass SysEx and points to the RemoteFish app.

## Digiface USB channels
| Item | Value | Tag |
| --- | --- | --- |
| Optical ports | 4 in, 4 out (TOSLINK), each ADAT or SPDIF; inputs detect the format, outputs are set per port | documented (guide, ch. 4, 25, Settings) |
| ADAT channels at 44.1/48 kHz | 32 in, 32 out (8 per port) | documented (ch. 25) |
| at 88.2/96 kHz (S/MUX) | 16 in, 16 out (4 per port) | documented |
| at 176.4/192 kHz (S/MUX4) | 8 in, 8 out (2 per port) | documented |
| Phones | analog output, channels 33/34 | documented (ch. 25) |
| TotalMix | 32 inputs, 34 software playback, 34 hardware outputs ("66 x 34 matrix") | documented (ch. 19.2) |
| Channel names | ADAT (1-8), (9-16), (17-24), (25-32); double speed (1-4), (5-8), (9-12), (13-16); quad speed (1+2), (3+4), (5+6), (7+8) | documented (ch. 8.3, 9.2) |
| Channel to port: names fill port 1, then 2, 3, 4 | implied by the names, not stated as "optical 1" | assumed |
| SPDIF on a port takes which of the eight slots | not stated in what was read | assumed: unknown, not modelled |

WhiskerMix: `src/mixer/AdatLayout.h` holds the table (channels per port, port of a channel, strips that exist at a rate); `tests/adat_layout_test.cpp` checks it. The owner layout (ADAT 1 to 16 plus phones) fits at 44.1 to 96 kHz. At 96 kHz its 16 channels spread over all four ports (4 per port), so the Pulse needs all four ADAT cables at 96 kHz. At 176.4/192 kHz only ADAT 1 to 8 exist: strips on 9 to 16 would be unavailable, the phones strips stay.

## TotalMix FX concepts (documented, guide ch. 19 to 21)
- Three rows: hardware inputs, software playback, hardware outputs. A control room section (Main, Speaker B, phones) sits beside them.
- Submix view (default): clicking a hardware output makes it the current submix. Every input and playback fader then sets the level sent to that output. The same input can go to several outputs at different levels. Up to 17 independent stereo submixes.
- Loopback (per hardware output): the output signal is sent to the record software instead of the hardware input of that channel; the input still reaches TotalMix.
- Record path is not changed by TotalMix, except in loopback.
- Model in WhiskerMix: `RoutingMatrix` (gain from every strip to every bus), buses = control room first, then hardware outputs. Loopback is not modelled yet (idea only).

## What was captured, and the known open points (owner capture)
Strip gains, pan, phase, stereo link, M/S, main volume, dim, speaker B were decoded from the vendor mixer's USB traffic. Which submix a fader write belongs to is not in those captures yet. WhiskerMix therefore only drives the Main submix in hardware; everything else in the matrix is done on the computer (software path). That is the honest limit of the routing page today. Status: untested on hardware.

## USB ID: needs the owner's `lsusb`
The kernel quirk for the Digiface USB matches `2a39:3f8c` (documented, kernel patch of Sept 2024). The code, udev rule and diagnostic script used `2a39:3fa0` (owner capture note, DIGIFACE-PROTOCOL.md). The companion page [ROUTING.md](ROUTING.md) reports from mailing list posts that 0x3fa0 was added as an alternate id in a later kernel patch; that was not verified against a kernel release. So both ids are plausible for one product. The code now accepts both and the udev rule lists both. Untested on hardware. Please run `lsusb | grep 2a39` and note which one shows up.

## Linux: what is possible
| Topic | What is documented | Tag |
| --- | --- | --- |
| Audio, clock, sample rate | Kernel 6.10 added the mixer quirk, basic audio support came with the same 2024 series; a user report says it works from 6.12. Exposed ALSA controls: Input 1-4 Sync, Format, Rate (read only), Output 1-4 Format (ADAT or SPDIF, read/write), Sync Source (Internal or Input 1-4), Current Sync Source, System Rate, Current Rate | documented (kernel patch) |
| Matrix mixer, output gain, level meters | "not implemented"; the patch says it is left to userspace using raw USB control messages | documented (kernel patch) |
| TotalMix on Linux | RME ships no TotalMix for Linux. The guide lists only Windows and macOS drivers (ch. 3) | documented |
| Class compliant (CC) mode | Guide: a firmware flash option "CC Mode Active" switches the unit to class compliant mode; the FAQ says to use it for Linux and to set the clock source (Internal, Optical 1-4) in the ALSA mixer. The guide does not say what TotalMix does in CC mode; that the hardware mixer is gone in CC mode is assumed (reported for other RME units, not confirmed for the Digiface) | documented / assumed |
| WhiskerMix hardware mixer writes | Raw USB writes learned from Windows captures. They need the normal (non-CC) firmware. Not run on a Linux machine yet | owner capture, untested on hardware |
| Number of ALSA channels | community reports about 34 (32 + phones); not checked here | assumed |

Result for routing on Linux: use ALSA/PipeWire graph routing for everything the hardware mixer path does not cover (that is what the software path in WhiskerMix does). Do not expect the vendor mixer's file formats or OSC. If the owner flashes CC mode, the hardware writes will probably not apply and WhiskerMix stays a software mixer; the clock source then comes from the ALSA control "Sync Source".

## Digiface MIDI remote (documented, guide ch. 22)
This is TotalMix being controlled over MIDI (Mackie Control protocol and simple CC/notes). It needs the TotalMix program running on Windows or macOS, so it does not give a Linux control path. Listed so nobody invents values:
- Main out volume: Control Change volume on MIDI channel 1.
- Notes on channel 1: Dim 0x5D, Mono 0x2A, Talkback 0x5E, Recall 0x5F, Speaker B 0x32, Cue Main 0x3E, Cue Phones 1 0x3F, Phones 2 0x40, Snapshots 1 to 8 0x36 to 0x3D, Trim 0x2D, Master mute 0x2C, Master solo 0x2B.
- Faders: `Bx yy zz`, controllers 102 to 117 (0x66 to 0x75), rows by MIDI channel (inputs 1 to 4, playback 5 to 8, outputs 9 to 12); example `B0 66 68` = input 1 at 0 dB.
All of the above: documented (guide ch. 22.5), untested here and not used by WhiskerMix.

## Ferrofish Pulse 16 DX
| Item | Value | Tag |
| --- | --- | --- |
| Analog | 16 in, 16 out | documented |
| ADAT | 4 in + 4 out ports; 8 ch per port at 32/44.1/48 kHz (32+32), 4 per port at 64/88.2/96 kHz (16+16), 2 per port at 128/176.4/192 kHz (8+8) | documented (DX guide v2.0c, I/Os - ADAT). Older Pulse16 guide says S/MUX4 over ADAT needs the MADI MX option; the DX guide does not repeat that |
| Clock | Master, or follow ADAT / MADI / Dante / BNC | documented (Clock menu) |
| Routing | matrix in blocks of eight channels, set on the unit or by remote | documented (Routing menu) |
| Presets | six slots (gains, levels, routing) | documented (Preset menu) |
| Remote | 5-pin MIDI (interface must pass SysEx), MIDI over MADI, RemoteFish app (Mac/PC); RemoteFish over Dante from firmware 3 | documented |
| Which messages the unit understands | NOT documented by Ferrofish in any manual read | not documented |
| SysEx frame, gains, levels, routing, clock, rate, presets, headphones | taken from RemoteFish 2.1 captures, firmware 5.04 | owner capture, untested on hardware (see PULSE16-PROTOCOL.md for each command) |
| MIDI routing, MADI 96k frame, MADI short frame, BNC termination, delay compensation, meter scale | real commands unknown | UNVERIFIED. Needs an owner capture or Ferrofish confirmation. Not mapped |

Per-channel routing on the Pulse is per block of eight, not per channel (documented). WhiskerMix's Pulse page follows that: it never offers per-channel routing on the unit.

## How the two fit together (the owner's studio)
- Cables: Digiface optical out N to Pulse ADAT in N, Pulse ADAT out N to Digiface in N (assumed layout, N = 1 to 4). Only ports 1 and 2 matter at 44.1/48 kHz for 16 channels; at 88.2/96 kHz all four carry 4 channels each.
- Pulse routing columns for ADAT: ADAT IN 1-8 .. 25-32 are columns 2 to 5 (owner capture). At double speed the block names stay the same but hold fewer channels: assumed, not captured.
- Clock: one master only. Default in WhiskerMix docs: Pulse on Master, Digiface follows ADAT (it is the documented "slave to the optical input" mode; the Linux ALSA control "Sync Source" does the same).
- A rate change changes the channel count on both sides. WhiskerMix now has the table for it; the UI does not hide strips yet (design idea only).

## Documented vs assumed vs untested, short
- Documented: channel counts and S/MUX, TotalMix concepts, Digiface MIDI remote values, Pulse features and remote options, kernel mixer controls, kernel lacks matrix/gain/meters.
- Assumed: port order, SPDIF slot use, CC mode drops the hardware mixer, DX needs no MADI option for ADAT S/MUX4, block names at double speed.
- Untested on hardware: every USB write to the Digiface, every SysEx to the Pulse, both USB IDs, Linux audio path with this unit.
- Not documented by the makers: the Pulse 16 SysEx messages and the Digiface USB vendor control messages. They come from captures only.
