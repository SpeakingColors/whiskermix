# Routing: one Digiface USB and one Pulse 16 DX on ADAT (Linux)

Status: design phase. Everything here is computed by `src/routing/AdatRouting.h` (unit tested, `sh tests/run.sh`) and has not been run against real hardware. Not built here: the JUCE app and the PipeWire adapter were not compiled for this change, only the pure C++ tests. RME TotalMix is not available on Linux; WhiskerMix replaces the parts it can (see `docs/DIGIFACE-PROTOCOL.md`).

Status words: DOCUMENTED = stated in a manufacturer document or kernel source (named below). ASSUMED = reasoned, not stated by a source. UNTESTED = not tried on the owner's hardware.

## Channel counts per sample rate
| Sample rate | Class | ADAT per optical port | ADAT channels per direction (4 ports) | Status |
| --- | --- | --- | --- | --- |
| 44.1 / 48 kHz | single | 8 | 32 | DOCUMENTED |
| 88.2 / 96 kHz | double, S/MUX | 4 | 16 | DOCUMENTED |
| 176.4 / 192 kHz | quad, S/MUX4 | 2 | 8 | DOCUMENTED |

- Each Digiface optical port carries either ADAT or S/PDIF (stereo), so S/PDIF uses up an ADAT port. DOCUMENTED.
- The only analog connection on the Digiface USB is the phones output (TRS). There are no analog inputs. The vendor mixer numbers it as output 33/34 (hardware index 32/33 in WhiskerMix, CONFIRMED from captures).
- Source: RME Digiface USB product page and the Digiface USB User's Guide (chapters on channel count under ASIO and Core Audio), figures as quoted by search results and a mirror of the guide (manuals.plus). The PDF itself could not be opened when this was written: re-read the guide, version and page, before relying on the per-port numbers.
- ASSUMED: at 88.2/96 kHz the first 16 hardware channels stay active, at 176.4/192 kHz the first 8. The kernel quirk (`snd-usb-audio`, added in kernel 6.12 era patches by Asahi Lina, extra PID 0x3fa0 in a follow-up) defines the real formats per rate. Run `sh tools/wm-hw-routing.sh` to print the channel counts ALSA reports per rate and correct this table if it differs.
- Pulse 16 DX: the Ferrofish documents list 16 channels up to 96 kHz and 8 at the quad rates (S/MUX4). How the DX numbers its S/MUX channels per ADAT port is UNKNOWN to this repo, so WhiskerMix only maps Digiface channel k to Pulse ADAT channel k at 44.1/48 kHz (straight cable, port 1 to port 1; ASSUMED) and reports "unknown" above that.

## Digiface on Linux: what is class compliant
The Digiface USB is not a class compliant (UAC2) device; it needs the vendor quirk in `snd-usb-audio` (Linux kernel patches from September 2024, product id 0x3f8c, with 0x3fa0 added as an alternate id in December 2024; read from the mailing list posts only, whether and in which release 0x3fa0 was merged was not checked). Audio, rate and clock are handled by the kernel and PipeWire; the hardware mixer (gains, main volume, dim) is WhiskerMix's USB path (`docs/DIGIFACE-PROTOCOL.md`). Check your kernel with `sh tools/wm-hw-routing.sh`.

## Clock: one master only
`planClock()` in `AdatRouting.h`:
- **Digiface leads (default).** The audio system picks the rate (Settings, Devices, Sample rate); the Pulse is set to clock source ADAT (`01 01 01`, CONFIRMED command). The Pulse shows "no lock" until ADAT arrives (Pulse notes).
- **Pulse leads.** Pulse clock source Master (`01 01 00`) and the master rate (`01 02 vv`, CONFIRMED, sent explicitly). Open the audio system at the same rate. The Digiface must follow its ADAT input; WhiskerMix has no command for the Digiface clock source, so this may need the vendor mixer once (UNTESTED).
- Ferrofish manual (Pulse 16 series, search result summaries of v2.0c and v1.1): the sample rate can only be set when the Pulse is master. Not read in full.
- Rates the Digiface lists and the Pulse accepts: 44.1, 48, 88.2, 96, 176.4, 192 kHz. 32, 64 and 128 kHz are refused by the plan. In Settings > Devices the Sample rate list now includes 176.4 and 192 and a Clock master choice; the choice is stored (`clkMaster`) but not yet sent to the units. The plan function is ready for that wiring.
- On changing rate, ADAT strips beyond the active channels are reported by `unavailableStrips()`; they are never deleted, so a workspace saved at 48 kHz comes back intact.

## PipeWire
- Use the Pro Audio profile for the Digiface so each channel is a separate port (`pw-cli ls Device`, then `pw-cli s <id> Profile '{ index: N, save: true }'`, per the PipeWire guides). Ports are then called like `playback_AUX0`. The exact node and port names for your system are not known here: read them with `pw-link -i` and `pw-link -o`.
- PipeWire documents that in Pro Audio mode the nodes of one device share a clock, so linking capture to playback of the same device needs no resampling (pipewire-devices(7)).
- `wm::routing::busLinkCommands()` turns the mixer buses into `pw-link` lines (Digiface hardware index n becomes `...playback_AUX<n>`, the 0-based ALSA channel; ASSUMED, check with the script). Phones are skipped because their ALSA position is not known.
- `wm::routing::channelNames()` gives names (ADAT 1 to ADAT n) for the WirePlumber rule in `src/names/NameSync.h`; channels beyond the ADAT count are named Ch n because the kernel's order for S/PDIF and phones is not known.
- Loopback or combine nodes (`libpipewire-module-loopback`, `capture.props` with `audio.position = [ AUX0 AUX1 ]`) can present a stereo pair of the Digiface to ordinary apps. Not set up by WhiskerMix.
- JACK names follow the PipeWire port names; not checked.

## Saved state
Strips keep their Digiface hardware channel in the workspace (`hw`, `pb`, tolerant of missing keys). The routing matrix is restored only when its size is sane (0 to 256 per side); a corrupt file is refused and the current routing stays. Rate and clock master are app settings (`rate`, `clkMaster`).

## Not done
- Sending the clock plan to the Pulse from the UI, and following rate changes from PipeWire.
- Any Pulse 16 DX ADAT port to S/MUX channel mapping above 48 kHz.
- Real-hardware checks of every statement above.
