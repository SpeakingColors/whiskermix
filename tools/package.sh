#!/bin/bash
# Usage: tools/package.sh <linux|windows|macos> <build-dir>   -> dist/WhiskerMix-<os>.zip
set -e
os=$1; b=$2; root="$(cd "$(dirname "$0")/.." && pwd)"
d="$root/dist/WhiskerMix-$os"; rm -rf "$d"; mkdir -p "$d/plugins" "$d/manual" "$d/assets"
cp "$root/packaging/common/INSTALL-$os.txt" "$d/INSTALL.txt"
cp "$root/LICENSE" "$d/"
cp "$root/docs/MANUAL.md" "$d/manual/"; [ -d "$root/docs/screenshots" ] && cp -r "$root/docs/screenshots" "$d/manual/"
cp "$root/assets/"* "$d/assets/"
for p in ShimmerDelay ShimmerReverb; do cp -r "$b/${p}_artefacts/"*/VST3/*.vst3 "$d/plugins/"; done
case $os in
  linux)
    mkdir -p "$d/app" "$d/udev"
    cp "$b/WhiskerMix_artefacts/"*/WhiskerMix "$d/app/"
    for p in ShimmerDelay ShimmerReverb; do cp "$b/${p}_artefacts/"*/Standalone/$p "$d/app/" ; done
    cp "$root/packaging/udev/"* "$d/udev/"
    cp "$root/packaging/linux-install.sh" "$d/install.sh"; cp "$root/packaging/linux-uninstall.sh" "$d/uninstall.sh"
    chmod +x "$d/install.sh" "$d/uninstall.sh" "$d/app/"* ;;
  windows)
    cp "$b/WhiskerMix_artefacts/Release/WhiskerMix.exe" "$d/"
    for p in ShimmerDelay ShimmerReverb; do cp "$b/${p}_artefacts/Release/Standalone/$p.exe" "$d/plugins/"; done ;;
  macos)
    cp -r "$b/WhiskerMix_artefacts/Release/WhiskerMix.app" "$d/"
    for p in ShimmerDelay ShimmerReverb; do cp -r "$b/${p}_artefacts/Release/Standalone/$p.app" "$d/plugins/"; done ;;
esac
(cd "$root/dist" && python3 -c "import shutil,sys;shutil.make_archive('WhiskerMix-$os','zip','.', 'WhiskerMix-$os')")
echo "dist/WhiskerMix-$os.zip"
