#!/bin/sh
# Builds and runs every test. Usage: sh tests/run.sh
set -e
cd "$(dirname "$0")/.."
for t in digiface_codec_test names_test mixer_test state_test engine_test reverb_test; do
  g++ -std=c++17 -Wall -pthread -o /tmp/wm_$t tests/$t.cpp
  /tmp/wm_$t
done
