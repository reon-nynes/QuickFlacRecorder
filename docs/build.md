# Build and package

Use CMake 3.22 or later, a C++20 compiler, Python 3, and Git. The project fetches JUCE 9.0.3 during configuration. A first configure needs network access. To use a local checkout or the JUCE copy in a package's source archive, add `-DFETCHCONTENT_SOURCE_DIR_JUCE=<absolute path to JUCE>` to the configure command. Preserve the pinned version. Set `-DQFR_VERSION=1.2.3` or `-DQFR_VERSION=1.2.3-rc.1` to reproduce a release's application metadata. An ordinary preview build defaults to `1.0.0`. `SOURCE.txt` in a preview package or release support download records its exact version and source commit. See [releases.md](releases.md) for the accepted tag format and platform version fields.

All platforms build the `QuickFlacRecorder` GUI and the `RecorderSessionTests`, `RecorderTranslationTests`, `RecorderPresentationTests`, and `RecorderUiTests` targets. Run `ctest` with `--no-tests=error` so a missing test suite cannot appear to pass. Session tests use controlled audio and a real temporary target folder. Localization tests use the public translation/resolver and adapter-input boundaries; UI tests exercise native JUCE controls with isolated settings. Ordinary CTest does not establish real microphone behavior. The optional Windows hardware/render check and contributor procedure are in [localization.md](localization.md).

## Windows x64

Install Visual Studio 2022 or its Build Tools with the Desktop development with C++ workload, the Windows SDK, CMake, and Python 3. The CMake configuration selects the static MSVC runtime for the application, tests, and JUCE build. There is no separate FLAC runtime installation.

From the repository in PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON
cmake --build build --config Release --target QuickFlacRecorder RecorderSessionTests RecorderTranslationTests RecorderPresentationTests RecorderUiTests --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error
& .\build\QuickFlacRecorder_artefacts\Release\QuickFlacRecorder.exe
```

Commit the source used for the build before creating a source-matched preview:

```powershell
.\scripts\package-windows.ps1 -BuildDirectory build
```

The script finds `dumpbin` through the installed Visual Studio tools. It checks x64 machine type, rejects dynamic C/C++ runtime imports, and requires other imported DLLs to be Windows system libraries. It writes the executable, inspection output, launch instructions, licenses, source, and a SHA-256 hash into `dist/QuickFlacRecorder-windows-x64.zip`. This checks imports from the built executable. Native launch and actual input-device behavior still require acceptance checks on Windows 10/11.

If JUCE came from a local checkout, pass `-JuceSource <path>` to the script. If Python is not named `python` in your environment, pass `-Python <absolute path to python.exe>`.

## macOS universal

Install Xcode's command-line tools, CMake, Ninja, and Python 3. Configure both slices with a macOS 12.0 deployment target:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  '-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64' -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build build --target QuickFlacRecorder RecorderSessionTests RecorderTranslationTests RecorderPresentationTests RecorderUiTests --parallel 2
ctest --test-dir build --output-on-failure --no-tests=error
open build/QuickFlacRecorder_artefacts/Release/QuickFlacRecorder.app
bash scripts/package-macos.sh build
```

`ctest` runs the slice native to the host. The package script verifies both binary architectures, checks each slice's deployment target is 12.0, requires only system dynamic-library imports, and verifies a microphone usage description is present. It adds notices to the app bundle, applies an ad-hoc signature for Apple Silicon loading, and verifies that signature. This does not authenticate a publisher or notarize the app. It produces `dist/QuickFlacRecorder-macos-universal.zip` with the inspection report and corresponding source. Real recording must be checked on both Intel and Apple Silicon, including microphone permission behavior.

## Linux x64

The CI host is `ubuntu-24.04`. The build runs inside `ubuntu:22.04` to retain the initial supported baseline. For a native Ubuntu 22.04 build, install the same prerequisites:

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git ca-certificates curl python3 \
  pkg-config libasound2-dev libjack-jackd2-dev libfreetype6-dev libfontconfig1-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev libxcursor-dev libxi-dev libgl1-mesa-dev xvfb xauth file
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target QuickFlacRecorder RecorderSessionTests RecorderTranslationTests RecorderPresentationTests RecorderUiTests --parallel 2
xvfb-run -a ctest --test-dir build --output-on-failure --no-tests=error
bash scripts/package-linux.sh build
```

JUCE 9.0.3 enables XInput by default. Ubuntu's [`libxi-dev` package](https://packages.ubuntu.com/jammy/amd64/libxi-dev/filelist) supplies its required `X11/extensions/XInput2.h` header. The fresh runtime baseline also installs [`libxi6`](https://packages.ubuntu.com/jammy/amd64/libxi6/filelist) so JUCE can load `libXi.so.6` and exercise that input path.

Packaging uses versioned linuxdeploy and appimagetool assets with checked SHA-256 hashes. It obtains the AppImage runtime through an immutable GitHub release asset ID and verifies its hash. `GH_TOKEN` is optional and raises the GitHub API rate limit; Actions supplies its read-only workflow token. The script does not use the appimagetool default that fetches the latest runtime.

The AppImage bundles its ELF dependencies and the ALSA library. JACK is optional and uses the host's compatible JACK library, including a PipeWire JACK implementation where available. The desktop's sound configuration, access to audio devices, display, and graphics drivers remain host responsibilities. The package contains copied-library copyright notices and exact source-package references. It produces `dist/QuickFlacRecorder-linux-x64.tar.gz`, containing the AppImage, instructions, licenses, source archive, and hash.

The separate `linux-baseline` job extracts that package in a fresh Ubuntu 22.04 container with desktop runtime libraries and inspection tools, without build development packages. It rejects unresolved executable dependencies and bundled ELF files requiring GLIBC newer than 2.35. It starts the packaged GUI under Xvfb and requires it to remain running. Extraction avoids mounting FUSE inside CI. This establishes a baseline dependency/startup check when the job passes. It does not establish audio capture or full distribution compatibility.

To run the same check in an Ubuntu 22.04 runtime environment:

```bash
bash scripts/verify-linux-baseline.sh dist/QuickFlacRecorder-linux-x64.tar.gz
```

## Source and verification records

Every package script runs `scripts/verify-translations.py` against the packaged recorder binary and writes `localization-resources.txt`. It requires the shared registry and every registered catalog to match the embedded source bytes, allowing platform line endings. macOS also checks each localized microphone explanation in the bundle. The Linux baseline repeats the catalog inspection after AppImage extraction. The formatter has no additional runtime/data dependency. Catalog validation and rendering checks remain separate from this packaging inspection.

Long recordings keep a fixed capture buffer of 262144 frames and a bounded codec block. The writer thread flushes encoded bytes to the native disk stream every five seconds with `FlushFileBuffers` on Windows and `fsync` on macOS/Linux. The deadline uses a monotonic clock and also runs while audio continues to arrive. Encoding and flushing stay off the audio callback. Storage failures receive three retries following the triggering failure, with one second between retries and a ten-second no-progress limit. Writes retry before failure reaches the FLAC encoder and resume from the unwritten bytes. Exhausted storage recovery retains an incomplete `.partial.flac`. Recovered storage errors produce an accompanying recording log and a saved-with-errors outcome after successful finalization. See [fault recovery validation](validation/fault-recovery.md).

Periodic flushing leaves the FLAC encoder running. The codec's final buffered audio and STREAMINFO metadata finish at Stop, followed by validation and promotion. A periodically flushed active file remains an incomplete recording until that finalization succeeds. The session's `Options::diskFlushInterval` is an internal durability policy with a five-second application default; tests shorten it to observe multiple real-stream flushes and file growth without waiting through long captures. It is not a separate user setting.

Native failures retain their operation, platform error code, and original platform message separately from input diagnostics and adapter exceptions. Finalization and promotion preserve the first native disk diagnostic. Final-name promotion atomically refuses replacement using `MoveFileExW` on Windows, `renamex_np(RENAME_EXCL)` on macOS, or `renameat2(RENAME_NOREPLACE)` on Linux. It does not require hard links. Filesystems without supported exclusive renaming retain the incomplete recording and report the failure. The [recording reliability record](validation/reliability.md) includes the actual Linux exFAT FUSE test, native Windows readiness checks, and remaining platform checks.

The packaging scripts require a clean source repository, including no untracked files outside ignored build paths, because their source archive uses `git archive HEAD`. The source packager verifies that Git's repository root matches the source root and grants Git command-scoped ownership trust only for that exact directory. This handles container-mounted workspaces without changing global Git configuration. Git recognition failures are reported separately from uncommitted source changes. Archive creation disables Git's automatic CRLF conversion for that command so the package retains canonical source bytes and runnable LF shell scripts on every platform. Rebuild after changing application or dependency source. Do not describe a package as source-matched if it came from an earlier build. A local JUCE override must point at the same JUCE source used to compile the app. Shell package scripts accept build, destination, and JUCE-source paths as their first, second, and third arguments.

The [Actions workflow](../.github/workflows/preview.yml) configures, builds, tests, inspects, and packages each platform. Branch and PR artifacts remain unsigned previews retained for 30 days. On a valid `v*` tag push, application metadata comes from that tag and a publishing job waits for every platform and Linux baseline check before publishing all three portable packages and their three support downloads. For tag pushes, `scripts/release.py package-app` splits the inspected preview archive before manifest generation. Each portable archive contains only the executable, macOS app bundle, or AppImage; the matching `-support.zip` retains source, licenses, launch instructions, hashes, and inspection evidence. Preview archives keep their full contents. Only the publishing job has repository write permission. The tag push is the owner's release approval, superseding the initial separate review requirement. The [release guide](releases.md) describes draft staging, archive hashes, and reruns that preserve public downloads.

Keep evidence for the exact source commit and binary hash in the [acceptance record](acceptance.md). Pending jobs and hardware checks remain pending until their reports exist.

## Tooling references

The accepted framework version remains JUCE 9.0.3. The package scripts follow [CMake's static-runtime setting](https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html), [GitHub's supported runner labels](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job), [AppImage baseline guidance](https://docs.appimage.org/introduction/concepts.html), [linuxdeploy's packaging process](https://github.com/linuxdeploy/linuxdeploy), and [appimagetool's explicit runtime option](https://github.com/AppImage/appimagetool/blob/1.9.1/README.md). These tooling references were checked on 2026-10-11.
