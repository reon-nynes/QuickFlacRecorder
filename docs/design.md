# Recorder design interview

Status: all design decisions accepted; the user requested publication through to-spec and confirmed the recording-session test boundary on 2026-10-11. The implementation specification is [GitHub issue #1](https://github.com/reon-nynes/QuickFlacRecorder/issues/1), labelled ready-for-agent, with a local copy in [spec.md](spec.md). Application implementation is the next task.

## User requirements

- Create a simple recorder with a slick, compact interface.
- Deliver a single executable and support multiple operating systems.
- Use JUCE 9.0.3 with CMake, as accepted in round 2.
- Include a target-folder field, defaulting to the user's Downloads folder.
- Include a save-name field based on date, time, and a counter.
- Start the counter at 01 and increment it after detecting names with the same prefix.
- Include input-driver and microphone selection.
- Put a live level meter beside the Record button.
- Save FLAC at the input device's sample rate and 16-bit depth.

## Repository facts

- Repository: https://github.com/reon-nynes/QuickFlacRecorder
- Existing files: .gitignore and a GNU GPL version 3 license.
- No application code or existing product design documents were present at the start of the interview.
- The repository is public. The local GitHub CLI is authenticated as reon-nynes with admin permission on it.
- The connected GitHub app uses a different account. Repository work should use the already authenticated local CLI.
- Windows C++ build tools, CMake, Ninja, and a Windows SDK are installed.

## Verified framework facts

Checked on 2026-10-11 against official sources.

- The current official JUCE release is [9.0.3](https://github.com/juce-framework/JUCE/releases/tag/9.0.3).
- JUCE provides [FLAC writing](https://docs.juce.com/master/classjuce_1_1FlacAudioFormat.html) with a bundled codec. Its [implementation](https://raw.githubusercontent.com/juce-framework/JUCE/master/modules/juce_audio_formats/codecs/juce_FlacAudioFormat.cpp) supports 16-bit output.
- The [open device's current sample rate](https://docs.juce.com/master/classjuce_1_1AudioIODevice.html) is available after device setup. Shared-mode Windows audio may perform operating-system conversion, so hardware-native recording is a separate promise from preserving the opened stream's rate.
- Current JUCE modules are dual-licensed under [AGPLv3 or the commercial JUCE license](https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md). The user accepted the AGPLv3 route in round 2. The repository license will change during implementation after the final design confirmation.
- [JUCE CMake app targets](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/CMake%20API.md) support desktop application builds. macOS apps use a bundle. Linux has [system dependencies](https://raw.githubusercontent.com/juce-framework/JUCE/master/docs/Linux%20Dependencies.md), so AppImage compatibility requires validation.
- [JUCE 9.0.3's platform requirements](https://raw.githubusercontent.com/juce-framework/JUCE/9.0.3/README.md) support Windows 10 version 1607+, macOS desktop apps, and mainstream Linux distributions. x64 and ARM targets exist, and macOS supports universal Intel/Apple Silicon binaries.
- The accepted macOS 12+ release minimum accounts for current [Xcode deployment targets](https://developer.apple.com/xcode/system-requirements). It is a product baseline choice, not JUCE's minimum.
- [AppImage guidance](https://docs.appimage.org/introduction/concepts.html) recommends building on the oldest supported target. An Ubuntu 22.04 build container on an Ubuntu 24.04 GitHub runner is a proposed way to retain that compatibility baseline. Direct [Ubuntu 22.04 runners](https://github.com/actions/runner-images/issues/14254) are deprecated and scheduled to retire on 2027-04-17.
- JUCE's optional [JACK backend](https://raw.githubusercontent.com/juce-framework/JUCE/9.0.3/modules/juce_audio_devices/native/juce_JackAudio.cpp) loads the JACK client library dynamically and expects a running server. PipeWire can provide [JACK compatibility](https://docs.pipewire.org/page_man_pw-jack_1.html). This should be tested on a representative Linux desktop.

## Design tree

Rounds 1, 2, and 3 are accepted. The decision frontier is empty. The user has accepted all recommendations and continued by requesting the implementation specification through to-spec. The published issue records the implementation contract and the confirmed testing boundary.

| Branch | Current decision | Choices that depend on it |
| --- | --- | --- |
| Audience and distribution | Public, free, AGPLv3 app in existing repository; implementation branch and draft PR | Settled |
| Recording workflow | One file per Record/Stop cycle, live idle meter and elapsed time | Settled |
| Input channels | Mono channel 1 by default; selectable mono channel or distinct stereo left/right channels | Settled |
| Filename meaning | Editable local date/time prefix retained across takes; highest occupied counter plus one; atomic reservation | Settled |
| Platform and packaging | Windows 10/11 x64 .exe, macOS 12+ universal .app, Ubuntu 22.04+ baseline Linux x64 AppImage | Settled |
| Framework | JUCE 9.0.3, CMake, AGPLv3 route | Settled |
| Audio backends and rate | WASAPI shared/exclusive and ASIO; CoreAudio; ALSA/optional JACK; preserve opened stream rate | Settled |
| Preferences | Remember folder, input/channel selections and window position; new prefix each launch | Settled |
| Interface | Dark 600 by 400 logical-pixel content area; centered 32-pixel timer, 140 by 64 Record/Stop action, larger type, explicit channels and peak meters | Settled, updated by #8 |
| Recording reliability | Stop on failures, preserve finalizable audio, .partial.flac until finalized, no silent overwrite | Settled |
| Validation and release | Focused automated checks and three-platform CI; Windows hardware check; unsigned previews; public release after review | Settled |

## Accepted in round 1

1. Publish a free, open-source recorder from the existing repository.
2. Record one continuous file per Record/Stop cycle. Show the meter before recording and elapsed time during recording. Leave pause, playback, editing, and speaker monitoring outside v1.
3. Support one selected mono input or a selected stereo pair, including microphones and USB audio interfaces.
4. Initialize an editable recording prefix from local date and time when the app opens, retain it across takes, and append the next counter found in the target folder. Example: 2026-10-11_14-30-00_01.flac, then 2026-10-11_14-30-00_02.flac.
5. Target Windows, macOS, and Linux with one portable download per OS. Use a Windows .exe, a macOS .app, and a Linux AppImage. Test Windows locally first.

The user accepted items 1-4 with "Yes to all" and item 5 with "Yes".

## Accepted in round 2

The user accepted items 6-12 with "Yes to all".

6. Adopt JUCE 9.0.3 and CMake, and change the project license to AGPLv3 to use JUCE's open-source route consistently.
7. Remember the target folder, audio selections, channel selection, and window position in the user's normal settings location. Default the first launch to Downloads. Generate a new date/time prefix on every launch. If a saved input is unavailable, show that state and require selection before recording.
8. Allocate one more than the highest counter for the exact prefix in the selected target folder. Pad to at least two digits and continue to 100 after 99. Do not reuse gaps or overwrite files. Show the next full filename as a preview; reject unsafe filename characters.
9. Stop on input loss or a recording/write failure, preserve captured audio that can be finalized, and show an explicit error. Do not claim a failed or incomplete file saved successfully. While recording, write to a .partial.flac name, then promote it to the final .flac name after successful finalization. Preserve incomplete files for inspection after a crash; automated crash repair is outside v1.
10. Use a dark native window with a 600 by 400 logical-pixel content area. Put folder and prefix rows above driver/device/channel controls, a centered 32-pixel monospaced timer in a dedicated 60-pixel-high area, and the live meter beside a 140 by 64 Record/Stop button below. Show the input rate, fixed 16-bit output, filename preview, and two-line status in the footer. Use 15-pixel body text, 14-pixel labels, and 13-pixel secondary text. Issue #8 replaces the original compact dimensions and typography with this approved baseline. No waveform or separate settings window in v1.
11. Lock folder, prefix, and audio selections during recording. Make Stop complete finalization before another take can start. Closing during recording offers Continue recording or Stop and quit. Space toggles recording when a text field or selector does not have focus.
12. Target Windows 10/11 x64, macOS 12+ with a universal Intel/Apple Silicon app, and Linux x64 with Ubuntu 22.04+ as the initial tested baseline. Defer other architectures and a broader Linux compatibility promise until tested.

## Accepted in round 3

The user accepted items 13-19 with "Yes to all".

13. Offer WASAPI shared, WASAPI exclusive, and ASIO on Windows, CoreAudio on macOS, and ALSA plus optional JACK on Linux. Start with the system's normal backend. Do not expose DirectSound or a separate native PipeWire backend in v1. PipeWire compatibility uses the available Linux paths and must be tested.
14. Write the actual rate reported by the opened input stream without application resampling. Prefer the device's current/default rate and show the negotiated rate in the UI. Do not add a sample-rate or buffer-size control in v1. Shared-mode operating-system conversion may occur; users who need direct hardware access can select an available exclusive or ASIO path.
15. Default to mono input channel 1. Provide a mono/stereo choice, with one channel selector for mono and separate left/right selectors for stereo. Reject selecting the same channel twice. Meter the selected channels with one or two peak bars in dBFS and a brief clipping indicator. Do not apply software gain, normalization, noise reduction, or other sound processing in v1.
16. Require an existing writable target folder before recording. If Downloads or the remembered folder is unavailable, explain the problem and require the user to choose another folder. Do not silently redirect recordings or create a missing path. Show microphone permission problems in the same actionable status area.
17. Treat matching final and .partial.flac names as occupied counters. Reserve the next name without replacing any existing file and recheck it at recording start, so multiple app instances cannot overwrite one another. Changing the folder or prefix recomputes the filename preview. Keep incomplete files and show their status, without automatic repair in v1.
18. Validate filename allocation and conflicts, 16-bit mono/stereo FLAC contents, sample rate, recording stop/finalization, and simulated input/write failures with focused automated checks. Use GitHub Actions to build/package all three platforms. Require a real microphone recording on Windows and list outstanding macOS/Linux hardware tests before claiming full recording validation.
19. Prepare local source, documentation, and a working Windows build first, then push an implementation branch and open a draft pull request in the existing repository. Produce unsigned preview artifacts initially and document platform launch requirements. The initial separate public-release review requirement was superseded by [issue #15](https://github.com/reon-nynes/QuickFlacRecorder/issues/15): a valid version-tag push approves publication after all platform and Linux baseline checks pass. Signing and macOS notarization require a later credential/setup step.

## Consolidated product behavior

The interface update approved on 2026-10-11 is recorded in [issue #8](https://github.com/reon-nynes/QuickFlacRecorder/issues/8). [DESIGN.md](../DESIGN.md) and the runtime theme own the implemented layout and typography. Record has a dot and restrained red emphasis; Stop has a square. Both retain explicit labels and stable size through hover, pressed, focus, disabled, recording, and finalizing states. Recording-time locks preserve control positions, and long paths and values remain usable through editing and full-value tooltips.

The app opens a compact dark window with target-folder and recording-prefix fields, audio driver and input-device selectors, a mono/stereo choice and explicit channel selectors, a filename preview, and a peak meter beside the Record button. The status area shows the negotiated sample rate and 16-bit FLAC, recording elapsed time, and actionable folder, device, permission, or write errors. The app remembers the folder, audio selections, and window position in the normal user settings location. Each launch supplies a new editable local date/time prefix.

The first launch uses the current system input with mono channel 1 and the user's Downloads folder. Recording requires an existing writable target folder and an available input with any required microphone permission. An unavailable remembered device requires explicit replacement. The driver list offers WASAPI shared/exclusive and ASIO on Windows, CoreAudio on macOS, and ALSA plus optional JACK on Linux. The app opens the selected input at its current/default rate where available, displays the actual negotiated stream rate, and writes that rate without application resampling. Shared audio may involve operating-system conversion.

Each Record/Stop cycle produces one 16-bit FLAC containing exactly the selected mono input or distinct stereo left/right channels. The app meters those selected channels before and during recording and briefly indicates clipping. It does not add software gain, normalization, noise reduction, monitoring, playback, editing, pause, a waveform, or sample-rate/buffer controls in v1.

The save-name field contains the prefix rather than the extension or counter. For example, the prefix `2026-10-11_14-30-00` yields `2026-10-11_14-30-00_01.flac`, then `_02.flac`. The next counter is one more than the highest occupied counter for that exact prefix in the chosen folder. Counters use at least two digits, so 99 is followed by 100. Both final `.flac` and `.partial.flac` names reserve their numbers; gaps are not reused. Changes to the prefix or folder update the preview, and recording start rechecks and reserves the name without replacing another app instance's file. Prefixes must form safe portable filenames.

During recording, folder, prefix, and audio selections are locked. Stop drains and finalizes the recording before enabling another take. The writer uses a `.partial.flac` filename and promotes it to the final name only after successful finalization without overwriting an existing final file. Input loss or a write/recording failure stops capture, preserves audio that can be finalized, and reports an error. Interrupted files remain available for inspection; automatic repair is outside v1. Closing mid-take offers Continue recording or Stop and quit. Space toggles recording when a field or selector is not being edited.

## Implementation and acceptance plan

- Pin JUCE 9.0.3, use CMake, and change the project license to AGPLv3 after final design confirmation.
- Keep recording memory bounded and move encoding and disk I/O off the audio callback. Treat buffer overflow or writer failure as a recording error rather than silently dropping samples.
- Verify filename matching and allocation, incomplete-file reservations, concurrent collisions, invalid names, and folder changes with focused checks.
- Record known mono/stereo test signals into FLAC, read them back, and check sample rate, channel ordering, 16-bit metadata, frame count, and expected sample quantization.
- Verify stop/finalization and simulated input/write failures, including the transition from a partial file to a final file.
- Build and package Windows x64, macOS universal, and Linux x64 through GitHub Actions. Use an Ubuntu 22.04 build container on a supported runner for the initial Linux compatibility baseline.
- Validate real microphone capture on Windows locally, including a saved FLAC. List outstanding macOS/Linux microphone, permission, and device tests in the draft pull request and preview documentation.
- Build the Windows app with bundled JUCE/FLAC and a static C++ runtime, then inspect dependencies to validate the single-executable goal.
- Push an implementation branch and attach a draft pull request in the existing repository. Provide unsigned preview artifacts and clear launch instructions. Public releases use the [version-tag approval policy](releases.md).

## Specification publication checkpoint

All 19 decisions are accepted. On 2026-10-11, the user resumed from the handoff with to-spec and confirmed the proposed recording-session test boundary. The resulting specification has 57 user stories and is published as [GitHub issue #1](https://github.com/reon-nynes/QuickFlacRecorder/issues/1) with the ready-for-agent label. Use that issue as the initial implementation contract. [Issue #15](https://github.com/reon-nynes/QuickFlacRecorder/issues/15) updates release approval and publication as described in [releases.md](releases.md).
