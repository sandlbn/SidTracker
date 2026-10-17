#!/usr/bin/env bash
# Release zip: firmware + original example songs (.sidt) and their .sid
# exports, laid out like the SD card. Every .sid is checked frame for frame
# against SIDTracker's player while it is written (sidcompare).
#   tools/release/make_release.sh VERSION IMAGE
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
version="$1"; image="$2"
name="SIDTracker-$version"
stage="$here/build/release/$name"
rm -rf "$stage" "$here/build/release/$name.zip"
mkdir -p "$stage/Firmware" "$stage/SIDTracker/Projects" "$stage/SIDTracker/SID"
cp "$here/build/$image" "$stage/Firmware/"
cp "$here/build/DUALDEMO.sidt" "$here"/build/examples/*.sidt "$stage/SIDTracker/Projects/"
# Blast Grid 64 soundtrack (Marcin Spoczynski, examples/blastgrid).
cp "$here"/examples/blastgrid/BG_*.sidt "$stage/SIDTracker/Projects/"
for s in "$stage"/SIDTracker/Projects/*.sidt; do
    b="$(basename "$s" .sidt)"
    "$here/build/oracle/sidcompare" "$s" --frames 6000 --out "$stage/SIDTracker/SID/$b.sid" --show 0
done
sed -e "s/@VERSION@/$version/g" -e "s/@IMAGE@/$image/g" "$here/tools/release/README.txt" > "$stage/README.txt"
cp "$here/LICENSE" "$stage/LICENSE.txt"
(cd "$stage" && zip -qr "../$name.zip" .)
echo "build/release/$name.zip:"
(cd "$here/build/release" && unzip -l "$name.zip" | tail -n +4 | head -n -2)
