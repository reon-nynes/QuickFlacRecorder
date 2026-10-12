#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
archive=$(realpath "${1:?Provide the Linux preview tar.gz}")
destination=$(dirname "$archive")
report="$destination/linux-baseline.txt"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
exec > >(tee "$report") 2>&1
echo 'AppImage inspection and GUI startup on a clean Ubuntu 22.04 runtime image'
cat /etc/os-release
tar -xzf "$archive" -C "$work"
image="$work/QuickFlacRecorder-x86_64.AppImage"
chmod +x "$image"
(cd "$work" && "$image" --appimage-extract > /dev/null)
appdir="$work/squashfs-root"
binary="$appdir/usr/bin/QuickFlacRecorder"
python3 "$repo/scripts/verify-translations.py" "$binary"
file "$image" "$binary"
file "$binary" | grep -q 'ELF 64-bit.*x86-64'
[[ -f "$appdir/usr/share/doc/QuickFlacRecorder/LICENSE" ]] || { echo 'Missing packaged license'; exit 1; }
export LD_LIBRARY_PATH="$appdir/usr/lib:${LD_LIBRARY_PATH:-}"
ldd "$binary" | tee "$work/dependencies.txt"
if grep -q 'not found' "$work/dependencies.txt"; then exit 1; fi
while IFS= read -r -d '' elf; do
    file "$elf" | grep -q 'ELF' || continue
    required=$(readelf --version-info "$elf" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -Vu | tail -n 1 || true)
    if [[ -n "$required" ]]; then
        newest=$(printf '%s\nGLIBC_2.35\n' "$required" | sort -V | tail -n 1)
        [[ "$newest" == GLIBC_2.35 ]] || { echo "$elf requires $required beyond the baseline"; exit 1; }
        echo "$(basename "$elf") requires at most $required"
    fi
done < <(find "$appdir/usr" -type f -print0)
# Extraction avoids requiring a FUSE mount inside CI. This checks startup, not a microphone.
xvfb-run -a bash -c '
    "$1/AppRun" &
    app_pid=$!
    trap "kill $app_pid 2>/dev/null || true" EXIT
    sleep 4
    kill -0 "$app_pid"
    echo "Packaged GUI remained running for the startup check"
' bash "$appdir"
echo 'Baseline dependency and GUI startup checks passed. Real device recording remains a separate acceptance check.'
