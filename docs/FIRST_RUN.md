# First run on Bazzite (after install)
1. `sh tests/run.sh` (core tests, no extras needed)
2. `cmake -B build -DWM_BUILD_APP=ON && cmake --build build` (needs pipewire and libusb dev headers; easiest inside a toolbox/distrobox)
3. Start the app: a node called "WhiskerMix" appears in `pw-link -io`
4. `sh tools/wm-diagnose.sh` for the Digiface and naming checks
Expect small compile fixes in `src/pipewire` and `src/app`: they were written without headers to compile against.
