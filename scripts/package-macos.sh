#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:-build}" && pwd)
destination=${2:-"$repo/dist"}
juce=${3:-"$build/_deps/juce-src"}
app="$build/QuickFlacRecorder_artefacts/Release/QuickFlacRecorder.app"
binary="$app/Contents/MacOS/QuickFlacRecorder"
package="$destination/QuickFlacRecorder-macos-universal"
mkdir -p "$package"
[[ -x "$binary" ]] || { echo "Missing Release app: $app" >&2; exit 1; }
python3 "$repo/scripts/package-common.py" "$package" --juce-source "$juce"
ditto "$app" "$package/QuickFlacRecorder.app"
packaged="$package/QuickFlacRecorder.app"
binary="$packaged/Contents/MacOS/QuickFlacRecorder"
resources="$packaged/Contents/Resources"
mkdir -p "$resources"
cp "$package/LICENSE" "$package/NOTICE.md" "$package/SOURCE.txt" "$resources/"
cp -R "$package/licenses" "$resources/"
report="$package/macos-inspection.txt"
{
    echo 'Universal architecture, deployment target, system-library and microphone-description inspection'
    lipo -info "$binary"
    for arch in x86_64 arm64; do
        lipo "$binary" -verify_arch "$arch"
        otool -arch "$arch" -l "$binary"
        minimum=$(otool -arch "$arch" -l "$binary" | awk '$1 == "minos" { print $2 }')
        [[ "$minimum" == '12.0' ]] || { echo "Unexpected $arch deployment target: $minimum" >&2; exit 1; }
        otool -arch "$arch" -L "$binary"
        while IFS= read -r library; do
            case "$library" in /System/Library/*|/usr/lib/*) ;; *) echo "Unbundled library: $library" >&2; exit 1;; esac
        done < <(otool -arch "$arch" -L "$binary" | tail -n +2 | awk '{print $1}')
    done
    permission=$(/usr/libexec/PlistBuddy -c 'Print :NSMicrophoneUsageDescription' "$packaged/Contents/Info.plist")
    [[ -n "$permission" ]] || { echo 'Missing microphone permission description' >&2; exit 1; }
    echo "Microphone usage description: $permission"
} > "$report"
# Ad-hoc signing supports Apple Silicon loading; this is not Developer ID signing or notarization.
codesign --force --deep --sign - "$packaged"
codesign --verify --deep --strict "$packaged"
shasum -a 256 "$binary" > "$package/SHA256.txt"
ditto -c -k --sequesterRsrc "$package" "$destination/QuickFlacRecorder-macos-universal.zip"
echo "Unsigned macOS preview: $destination/QuickFlacRecorder-macos-universal.zip"
