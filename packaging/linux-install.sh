#!/bin/sh
# WhiskerMix installer (Linux, per user)
set -e
here="$(cd "$(dirname "$0")" && pwd)"
opt="$HOME/.local/opt/whiskermix"
mkdir -p "$opt" "$HOME/.vst3" "$HOME/.local/share/applications" "$HOME/.local/share/icons"
cp -r "$here/app/." "$opt/"
cp -r "$here/plugins/." "$HOME/.vst3/"
cp "$here/assets/whiskermix-logo.svg" "$HOME/.local/share/icons/whiskermix.svg"
cat > "$HOME/.local/share/applications/whiskermix.desktop" <<D
[Desktop Entry]
Type=Application
Name=WhiskerMix
Comment=Mixer and MIDI hub for RME Digiface USB
Exec=$opt/WhiskerMix
Icon=whiskermix
Categories=AudioVideo;Audio;Mixer;
D
echo "Installing the Digiface USB rule (sudo needed once)..."
sudo cp "$here/udev/70-whiskermix-digiface.rules" /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
echo "Done. Replug the Digiface, then start WhiskerMix from your menu."
