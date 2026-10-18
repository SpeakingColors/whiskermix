#!/bin/sh
# Compile-check the JUCE UI headers against a JUCE checkout. Usage: sh tools/check-juce.sh /path/to/JUCE
J=${1:?path to JUCE}
cd "$(dirname "$0")/.."
echo '#include "src/app/AppView.h"' > /tmp/wm_chk.cpp
g++ -std=c++17 -fsyntax-only -I. -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
  -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_ALSA=0 -I"$J/modules" \
  $(pkg-config --cflags freetype2 x11 fontconfig 2>/dev/null) /tmp/wm_chk.cpp && echo "JUCE UI syntax OK"
