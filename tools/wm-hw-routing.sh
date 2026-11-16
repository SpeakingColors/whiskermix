#!/bin/sh
# Read-only check of how the Digiface USB shows up on this Linux system, per sample rate.
# Prints the real ALSA channel counts for 44.1 to 192 kHz and the PipeWire ports, so docs/ROUTING.md can be confirmed or corrected.
# Changes nothing. Usage: sh tools/wm-hw-routing.sh [alsa-card-number]
card="${1:-$(aplay -l 2>/dev/null | sed -n 's/^card \([0-9]*\):.*Digiface.*/\1/p' | head -n1)}"
[ -n "$card" ] || { echo "No Digiface card found by aplay -l (is it plugged in and is the kernel new enough?)"; exit 1; }
echo "== ALSA card $card"; uname -r
for r in 44100 48000 88200 96000 176400 192000; do
  for dir in playback capture; do
    if [ $dir = playback ]; then cmd=aplay; else cmd=arecord; fi
    out=$(timeout 3 $cmd -D hw:$card -r $r -f S32_LE --dump-hw-params -d 0 /dev/null 2>&1 | sed -n 's/^CHANNELS: //p' | head -n1)
    echo "$r Hz $dir: channels ${out:-not reported}"
  done
done
echo "== PipeWire ports (names to use with pw-link)"
pw-link -o 2>/dev/null | grep -i -E "digiface|whiskermix"
pw-link -i 2>/dev/null | grep -i -E "digiface|whiskermix"
echo "== Current links"; pw-link -l 2>/dev/null | grep -i -B1 -A1 -E "digiface|whiskermix"
echo "== Clock: current rate and quantum"; pw-metadata -n settings 2>/dev/null | grep -E "clock.(force-)?(rate|quantum)"
