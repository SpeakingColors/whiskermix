# First run on Bazzite (after install)
1. `sh tests/run.sh` (core tests, no extras needed)
2. `cmake -B build -DWM_BUILD_APP=ON && cmake --build build` (needs pipewire and libusb dev headers; easiest inside a toolbox/distrobox)
3. Start the app: a node called "WhiskerMix" appears in `pw-link -io`
4. `sh tools/wm-diagnose.sh` for the Digiface and naming checks
Expect small compile fixes in `src/pipewire` and `src/app`: they were written without headers to compile against.

# Windows or macOS (UI and mixing test, no Digiface USB control, no PipeWire)
1. Get JUCE 8.0.4 into `third_party/JUCE` (`git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git third_party/JUCE`)
2. `cmake -B build -DWM_BUILD_APP=ON -DWM_WINDOWS_TEST=ON` then `cmake --build build --config Release`
3. Audio uses the system device (WASAPI/ASIO on Windows, CoreAudio on macOS). macOS uses the same option.

# Known gaps

- Routing changes are saved in workspace files but not yet part of undo.
- Adding channels while audio runs is not supported yet (needs `prepare()` with audio stopped).
