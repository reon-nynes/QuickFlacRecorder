#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
build=$(cd "${1:-build}" && pwd)
destination=${2:-"$repo/dist"}
juce=${3:-"$build/_deps/juce-src"}
binary="$build/QuickFlacRecorder_artefacts/Release/QuickFlacRecorder"
package="$destination/QuickFlacRecorder-linux-x64"
[[ -x "$binary" ]] || { echo "Missing Release executable: $binary" >&2; exit 1; }
[[ $(uname -m) == x86_64 ]] || { echo 'The AppImage package targets Linux x64' >&2; exit 1; }
mkdir -p "$package"
python3 "$repo/scripts/package-common.py" "$package" --juce-source "$juce"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
download() {
    local url=$1 hash=$2 output=$3
    curl --fail --location --retry 3 "$url" -o "$output"
    printf '%s  %s\n' "$hash" "$output" | sha256sum --check --status
}
# Versioned upstream release assets and checked hashes, not mutable continuous tool downloads.
download 'https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage' \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d "$work/linuxdeploy.AppImage"
download 'https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage' \
    ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0 "$work/appimagetool.AppImage"
# The upstream runtime release moves. Its asset ID and hash identify the exact binary.
runtime_commit=8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa
runtime_url='https://api.github.com/repos/AppImage/type2-runtime/releases/assets/596078161'
headers=(-H 'Accept: application/octet-stream' -H 'X-GitHub-Api-Version: 2022-11-28')
if [[ -n ${GH_TOKEN:-} ]]; then headers+=(-H "Authorization: Bearer $GH_TOKEN"); fi
curl --fail --location --retry 3 "${headers[@]}" "$runtime_url" -o "$work/runtime-x86_64"
printf '%s  %s\n' 156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074 "$work/runtime-x86_64" | sha256sum --check --status
mkdir "$work/linuxdeploy" "$work/appimagetool"
chmod +x "$work/linuxdeploy.AppImage" "$work/appimagetool.AppImage"
(cd "$work/linuxdeploy" && ../linuxdeploy.AppImage --appimage-extract > /dev/null)
(cd "$work/appimagetool" && ../appimagetool.AppImage --appimage-extract > /dev/null)
appdir="$work/QuickFlacRecorder.AppDir"
alsa=$(ldconfig -p | awk '/libasound.so.2 .*x86-64/ && !found {print $NF; found=1}')
[[ -n "$alsa" ]] || { echo 'ALSA runtime library is required for packaging' >&2; exit 1; }
"$work/linuxdeploy/squashfs-root/AppRun" --appdir "$appdir" --executable "$binary" \
    --desktop-file "$repo/packaging/QuickFlacRecorder.desktop" --icon-file "$repo/packaging/QuickFlacRecorder.svg" \
    --library "$alsa"
notices="$appdir/usr/share/doc/QuickFlacRecorder"
mkdir -p "$notices"
cp "$package/LICENSE" "$package/NOTICE.md" "$package/LAUNCH.md" "$package/SOURCE.txt" "$notices/"
cp -R "$package/licenses" "$notices/"
mkdir -p "$package/licenses/Ubuntu"
source_list="$package/linux-library-sources.txt"
printf 'Bundled Ubuntu libraries and exact source retrieval commands\n\n' > "$source_list"
library_owner() {
    local library=$1 candidate record owner recorded_path
    local candidates=("$library" "$(readlink -f "$library")")
    for candidate in "${candidates[@]}"; do
        case "$candidate" in
            /usr/lib/*) candidates+=("${candidate#/usr}");;
            /lib/*) candidates+=("/usr$candidate");;
        esac
    done
    for candidate in "${candidates[@]}"; do
        # Ubuntu usrmerge aliases may differ from paths recorded in dpkg.
        [[ -e "$candidate" && "$candidate" -ef "$library" ]] || continue
        if record=$(dpkg-query -S "$candidate" 2>/dev/null); then
            while IFS= read -r record; do
                owner=${record%%: /*}
                recorded_path=${record#*: }
                if [[ "$recorded_path" == "$candidate" && "$owner" =~ ^[a-z0-9][a-z0-9+.-]*(:[a-z0-9][a-z0-9-]*)?$ ]]; then
                    printf '%s\n' "$owner"
                    return 0
                fi
            done <<< "$record"
        fi
    done
    echo "Cannot identify an exact Ubuntu package owner for $library" >&2
    return 1
}
# Preserve distro notices and identify the matching sources for every library linuxdeploy copies.
while IFS= read -r -d '' library; do
    name=$(basename "$library")
    original=$(ldconfig -p | awk -v name="$name" '$1 == name && /x86-64/ && !found {print $NF; found=1}')
    if [[ -z "$original" ]]; then
        # linuxdeploy may copy the real versioned file and add SONAME symlinks.
        original=$(find /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu -name "$name" -print -quit)
    fi
    [[ -n "$original" ]] || { echo "Cannot identify source package for $name" >&2; exit 1; }
    owner=$(library_owner "$original")
    source=$(dpkg-query -W -f='${source:Package}=${source:Version}' "$owner")
    printf '%s: %s\n  apt-get source %s\n' "$name" "$owner" "$source" >> "$source_list"
    copyright="/usr/share/doc/${owner%%:*}/copyright"
    [[ -f "$copyright" ]] || { echo "Missing copyright notice for $owner" >&2; exit 1; }
    cp "$copyright" "$package/licenses/Ubuntu/${owner//:/_}-copyright"
done < <(find "$appdir/usr/lib" -type f -name '*.so*' -print0)
cp /usr/share/common-licenses/GPL-* /usr/share/common-licenses/LGPL-* "$package/licenses/Ubuntu/"
curl --fail --location "https://raw.githubusercontent.com/AppImage/type2-runtime/$runtime_commit/LICENSE" \
    -o "$package/licenses/AppImage-runtime-LICENSE"
curl --fail --location 'https://raw.githubusercontent.com/linuxdeploy/linuxdeploy/1-alpha-20251107-1/LICENSE.txt' \
    -o "$package/licenses/linuxdeploy-LICENSE.txt"
printf '\nAppImage runtime source at the exact packaged revision:\nhttps://github.com/AppImage/type2-runtime/tree/%s\n' "$runtime_commit" >> "$source_list"
printf '\nGenerated AppRun source:\nhttps://github.com/linuxdeploy/linuxdeploy/tree/1-alpha-20251107-1\n' >> "$source_list"
cp -R "$package/licenses/." "$notices/licenses/"
cp "$source_list" "$notices/"
ARCH=x86_64 "$work/appimagetool/squashfs-root/AppRun" --no-appstream \
    --runtime-file "$work/runtime-x86_64" "$appdir" "$package/QuickFlacRecorder-x86_64.AppImage"
chmod +x "$package/QuickFlacRecorder-x86_64.AppImage"
(cd "$package" && sha256sum QuickFlacRecorder-x86_64.AppImage > SHA256.txt)
tar -czf "$destination/QuickFlacRecorder-linux-x64.tar.gz" -C "$package" .
echo "Linux preview: $destination/QuickFlacRecorder-linux-x64.tar.gz"
