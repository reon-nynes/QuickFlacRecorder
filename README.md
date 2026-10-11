# QuickFlacRecorder

QuickFlacRecorder is a compact desktop recorder that saves a selected mono input or stereo pair as 16-bit FLAC. It uses the opened input stream rate without application resampling. Shared audio drivers may still perform operating-system conversion.

Choose an existing target folder, check the recording prefix and next filename, select the audio driver and input device, then watch the level meter and press Record. Press Stop to finish the file. The app retains the recording prefix across takes and advances the counter without replacing existing files. A take keeps its `.partial.flac` name until finalization succeeds.

The supported delivery targets are Windows 10/11 x64, macOS 12 or later with one universal Intel/Apple Silicon app, and Linux x64 with an Ubuntu 22.04 baseline. The Linux baseline is a starting compatibility target. A successful build does not establish microphone, permission, or device behavior on every platform.

Preview downloads come from the [build and package workflow](https://github.com/reon-nynes/QuickFlacRecorder/actions/workflows/preview.yml). Open a successful run for the implementation commit and download its platform artifact. These previews have no publisher certificate or macOS notarization. Each contains its portable app, launch instructions, license notices, inspection report where applicable, and application/JUCE source archive. GitHub requires sign-in to download workflow artifacts and retains them for 30 days.

Follow [the preview launch instructions](docs/preview.md) for your platform. Use [the build guide](docs/build.md) to build from source or reproduce a package. [The acceptance checklist](docs/acceptance.md) separates automated checks from the real microphone and native UI checks still required for a build review. Its unchecked items remain pending until someone records evidence for that exact build.

The implementation follows [the v1 specification](docs/spec.md), [domain glossary](GLOSSARY.md), and accepted [JUCE licensing](docs/adr/0001-use-juce-under-agplv3.md) and [platform packaging](docs/adr/0002-package-per-platform.md) decisions. The owner reviews a working Windows build and the draft pull request before a public release.

QuickFlacRecorder is licensed under GNU AGPL version 3. See [LICENSE](LICENSE) and [the third-party notices](NOTICE.md). JUCE 9.0.3 supplies the interface, audio-device access, and FLAC codec. ASIO uses the GPLv3 alternative in the bundled Steinberg SDK license. There is no warranty.
