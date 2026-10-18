#!/bin/sh
rm -rf "$HOME/.local/opt/whiskermix" "$HOME/.vst3/ShimmerDelay.vst3" "$HOME/.vst3/ShimmerReverb.vst3" \
  "$HOME/.local/share/applications/whiskermix.desktop" "$HOME/.local/share/icons/whiskermix.svg"
sudo rm -f /etc/udev/rules.d/70-whiskermix-digiface.rules
echo "WhiskerMix removed."
