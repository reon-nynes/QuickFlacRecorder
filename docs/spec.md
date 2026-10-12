# QuickFlacRecorder v1 implementation specification

## Problem statement

The user needs a compact desktop recorder that captures a selected microphone or audio interface and saves one FLAC file per recording. Starting a recording should require little setup. The app should make the selected input, incoming level, target folder, and next filename clear before capture begins.

The recorder must preserve the opened input stream rate, write 16-bit FLAC, and prevent existing recordings from being overwritten. It should work on Windows, macOS, and Linux with one portable download per operating system. Recording failures must be visible, and captured audio that can still be finalized must remain available.

The repository currently has no application, build configuration, or tests. This specification defines the first working version and its delivery requirements.

## Solution

Build a free, public, open-source JUCE desktop app with a dark 600 by 400 logical-pixel content area plus the native title bar. Put target-folder and recording-prefix fields above audio driver, input device, and channel controls. Show a centered 32-pixel monospaced elapsed timer in a dedicated 60-pixel-high area. Below it, put the idle level meter beside a 140 by 64 Record/Stop button with a record-dot or stop-square icon and an explicit text label. Reserve footer space for the negotiated input stream rate, fixed 16-bit output, next filename, and two-line status.

Each Record/Stop cycle creates one mono or stereo FLAC in the target folder. The app generates a new editable local date/time recording prefix at launch and retains that prefix across takes. It appends a counter that is one greater than the highest occupied counter for that exact prefix. Both complete and incomplete recordings occupy counters. The app reserves names without replacing files and finalizes recordings before allowing another take.

Deliver a working local Windows build, automated behavior checks, build/package CI for all three platforms, unsigned preview artifacts, and an implementation branch with a draft pull request. A public release follows the user's review of the working build.

## User stories

1. As a person recording audio, I want a compact recorder window, so that I can keep it available while working in other applications.
2. As a person recording audio, I want a dark interface with a prominent Record/Stop control, so that I can recognize the recording action and current state quickly.
3. As a first-time user, I want the target folder to default to my Downloads folder, so that I can find my recordings without configuring a location first.
4. As a person recording audio, I want to enter or choose an existing target folder, so that I can save recordings where I need them.
5. As a person recording audio, I want the app to remember my target folder between launches, so that I do not need to choose it for every session.
6. As a person whose target folder is missing or unwritable, I want an actionable error before recording starts, so that I can choose a usable folder.
7. As a person recording audio, I want the app to require my chosen target folder, so that my recordings are not silently saved somewhere else.
8. As a person recording audio, I want a new recording prefix based on local date and time at each launch, so that recordings from a session have an identifiable starting name.
9. As a person recording audio, I want to edit the recording prefix, so that I can name a session for its subject or purpose.
10. As a person making several recordings, I want the recording prefix to remain the same across takes, so that those recordings stay grouped by name.
11. As a person recording audio, I want a preview of the next complete filename, so that I know what will be saved before pressing Record.
12. As a person recording audio, I want the filename preview to update when I change the target folder or recording prefix, so that it reflects the next recording in that location.
13. As a person recording audio, I want unsafe recording prefixes to be rejected with an explanation, so that the resulting filename works across the supported platforms.
14. As a person starting a new recording prefix, I want its counter to begin at 01, so that the first recording has a consistent name.
15. As a person making repeated recordings, I want each new counter to exceed the highest occupied counter for the exact prefix, so that previous recordings remain intact and gaps are not reused.
16. As a person making more than 99 recordings, I want counters to continue to 100 and beyond, so that the session can continue without renaming its prefix.
17. As a person with incomplete recordings in a target folder, I want their counters to remain occupied, so that a later take cannot overwrite audio left by an interrupted recording.
18. As a person using multiple recorder instances, I want each instance to reserve its recording filename safely at recording start, so that simultaneous takes cannot replace one another's files.
19. As a first-time user, I want the app to begin with the system's normal audio driver and current input device, so that I can start with the usual recording setup.
20. As a person recording audio, I want to select an audio driver separately from an input device, so that I can choose how the app accesses my microphone or audio interface.
21. As a Windows user, I want WASAPI shared, WASAPI exclusive, and ASIO choices where available, so that I can use the audio path appropriate to my equipment.
22. As a macOS user, I want CoreAudio input selection, so that I can record with the system's supported audio devices.
23. As a Linux user, I want ALSA and optional JACK input selection, so that I can use supported desktop audio configurations.
24. As a person recording audio, I want the app to remember my audio driver, input device, and channel selections, so that I can reuse my recording setup.
25. As a person whose remembered input device is unavailable, I want the app to show that state and require a replacement selection, so that it does not record an unintended input.
26. As a person whose microphone permission is missing, I want an actionable status message, so that I can grant access before recording.
27. As a first-time user, I want mono input channel 1 selected by default, so that a typical microphone recording starts with one explicit input channel.
28. As a person using a multichannel input device, I want to choose which channel supplies a mono recording, so that I capture the intended microphone or source.
29. As a person making a stereo recording, I want separate left and right input-channel selectors, so that the FLAC contains the intended stereo pair in the intended order.
30. As a person making a stereo recording, I want the app to reject the same input channel in both selectors, so that a stereo pair always uses two distinct channels.
31. As a person preparing to record, I want the level meter to respond before I press Record, so that I can verify the selected input is receiving audio.
32. As a person recording audio, I want one or two peak bars in dBFS for the selected channels, so that I can assess the incoming level for a mono input or stereo pair.
33. As a person recording audio, I want a brief clipping indicator, so that I can notice excessive input levels.
34. As a person recording audio, I want the app to show the negotiated input stream rate and fixed 16-bit FLAC output, so that I know the format of the saved recording.
35. As a person recording audio, I want the FLAC to use the actual opened input stream rate without application resampling, so that the app preserves that stream's rate.
36. As a person recording audio, I want the app to capture only the selected mono input or stereo pair without added sound processing, so that the saved audio represents those inputs with the required 16-bit conversion.
37. As a person recording audio, I want one continuous file per Record/Stop cycle, so that each take is easy to identify and manage.
38. As a person recording audio, I want elapsed time during capture, so that I can see the duration of the current take.
39. As a person recording audio, I want the target folder, recording prefix, and audio selections locked during a take, so that its save location and input setup stay consistent.
40. As a person stopping a recording, I want the app to finish writing and finalizing the FLAC before enabling another take, so that the previous recording is complete before the next starts.
41. As a person recording audio, I want an active recording to use a .partial.flac name until successful finalization, so that incomplete recordings can be identified in the target folder.
42. As a person whose input device disconnects, I want capture to stop with an explicit error and preserve audio that can be finalized, so that I know the take was interrupted and can inspect the captured audio.
43. As a person whose recording encounters a disk or writer failure, I want capture to stop and the failure to be reported, so that I do not mistake a failed recording for a successful save.
44. As a person whose recording exceeds the capture buffer or otherwise fails, I want the app to report an error, so that missing samples are not silently accepted as a complete recording.
45. As a person returning after a crash, I want incomplete recording files to remain available with a visible incomplete status, so that I can inspect them.
46. As a person closing the app during a take, I want Continue recording and Stop and quit choices, so that I can decide whether to keep capturing or finalize before exit.
47. As a person using keyboard controls, I want Space to toggle Record/Stop when I am not editing a field or selector, so that I can control capture without interfering with text entry or selection.
48. As a person reopening the app, I want it to remember its window position, so that it returns to my preferred workspace location.
49. As a Windows user, I want one portable x64 executable for Windows 10/11, so that I can run the recorder without a separate codec or C++ runtime installation.
50. As a macOS user, I want a portable app for macOS 12 or later with Intel and Apple Silicon support, so that I can use the same download on either architecture.
51. As a Linux user, I want an x64 AppImage tested against an Ubuntu 22.04 or later baseline, so that I have a documented initial compatibility target.
52. As a preview user, I want clear launch instructions for unsigned artifacts, so that I understand the steps required by my platform.
53. As the project owner, I want the source and license to follow JUCE's AGPLv3 route, so that the public free application has a consistent licensing decision.
54. As the project owner, I want automated evidence for filename safety, FLAC contents, finalization, and failure handling, so that I can assess recording correctness before relying on the app.
55. As the project owner, I want build/package CI for Windows, macOS, and Linux, so that each preview's platform build has a repeatable process.
56. As the project owner, I want real Windows microphone validation and an explicit record of outstanding macOS/Linux hardware checks, so that build success is not mistaken for full recording validation.
57. As the project owner, I want a working local Windows build and a draft pull request before a public release, so that I can review the implementation and try the recorder first.
58. As a person recording for hours, I want the app to flush captured audio to disk periodically during the take, so that the recording does not wait until Stop to commit its encoded audio to disk.

## Implementation decisions

- Issue #10 extends the normal recording flow to system-selected English and Simplified Chinese through embedded catalogs. Resolve current interface-language preferences once per launch, inject the immutable selection, preserve native data and control state independently of captions, and format structured successful session outcomes in presentation. [ADR 0003](adr/0003-system-interface-language-catalogs.md) and [the contributor procedure](localization.md) define the module contract. Error and recovery translation is the following slice.

- Use JUCE 9.0.3 pinned through CMake for the native interface, audio input, and bundled FLAC writing. Adopt AGPLv3 for the project and replace the existing GPLv3 license during implementation. Follow the accepted architectural decisions titled "Use JUCE under AGPLv3" and "Package a portable download for each desktop platform."
- Build a dark native window with a 600 by 400 logical-pixel content area. Arrange compact target-folder and recording-prefix rows above audio driver, input device, mono/stereo, and explicit channel selectors. Center the 32-pixel monospaced elapsed timer in its dedicated 60-pixel-high area. Put the live level meter beside the 140 by 64 Record/Stop button. Include the filename preview, negotiated input stream rate, fixed 16-bit output, and a reserved two-line status area. Use 15-pixel body/control/popup/status text, 14-pixel labels, and 13-pixel secondary text. Keep the canonical values and runtime theme synchronized with DESIGN.md.
- Give Record a dot and restrained red emphasis, Stop a square, and both explicit labels. Hover, pressed, keyboard focus, disabled, recording, and finalizing states must remain distinct and readable. Finishing... retains the action's size and disables another take until finalization completes. Long editable paths and full-value tooltips must remain usable; recording-time locks must not move controls.
- Introduce a recording-session module as the main behavioral boundary. It should coordinate recording configuration, filename preview and reservation, input-channel selection, capture, stop/finalization, and observable success or failure outcomes. Keep the native UI and audio-device integration connected to that behavior. Use the session boundary for the automated checks described below.
- Organize device access, filename allocation, FLAC writing, preferences, and UI responsibilities so the recording session can be exercised with known audio and controlled failures. Those responsibilities may be internal collaborators. Avoid adding separate test boundaries unless the accepted session boundary proves insufficient.
- On first launch, use the user's Downloads folder, the system's normal audio driver and current input device, and mono input channel 1. Store the target folder, audio driver, input device, channel selections, and window position in the normal user settings location. Generate a new recording prefix from local date and time on every launch rather than restoring the previous prefix.
- Require an existing writable target folder before capture starts. If Downloads or a remembered folder is missing or unusable, explain the problem and require another folder. Do not silently redirect recordings or create a missing path. Report microphone permission problems through the same actionable status area.
- Treat a missing remembered input device as unavailable and require an explicit replacement selection before recording. Keep audio driver and input device as separate concepts and selectors.
- Offer WASAPI shared, WASAPI exclusive, and ASIO on Windows, CoreAudio on macOS, and ALSA plus optional JACK on Linux. Start with the system's normal audio driver. PipeWire compatibility uses the available Linux audio paths and requires representative desktop validation.
- Open the selected input at its current/default rate where available. Read and display the actual negotiated input stream rate and use that rate for the FLAC without application resampling. Shared-mode operating-system conversion may occur. Do not describe the input stream rate as a guarantee of the physical hardware's native rate. Available exclusive or ASIO access is the choice for users who need direct hardware access.
- Support one selected mono input channel or a stereo pair with explicit left and right channel selections. Reject the same channel in both stereo selectors. Encode exactly the selected channels in the selected order at 16-bit depth.
- Meter the selected channels before and during recording. Use one peak bar for mono or two for stereo, in dBFS, with a brief clipping indicator. Do not apply software gain, normalization, noise reduction, or other added sound processing.
- The save-name field contains the recording prefix. The app appends the counter and extension. For example, the prefix `2026-10-11_14-30-00` produces `2026-10-11_14-30-00_01.flac`, then `2026-10-11_14-30-00_02.flac`. Retain the editable prefix across takes within a launch.
- Match occupied names for the exact recording prefix in the selected target folder. Allocate one more than the highest occupied counter, beginning at 01 when none exists. Pad counters to at least two digits, continue to 100 after 99, and never reuse gaps. Both final `.flac` and `.partial.flac` names occupy counters.
- Validate recording prefixes for safe portable filenames. Recompute the preview when the target folder or prefix changes. At recording start, recheck occupied names and reserve a name atomically without replacing an existing file. Handle concurrent instances without overwriting their recordings. A preview alone does not reserve a filename.
- Write an active take to a `.partial.flac` name. Stop must drain queued captured audio and complete finalization before another take can start. Promote the file to its final `.flac` name only after successful finalization, without replacing an existing final file.
- Keep recording memory bounded. Move encoding and disk I/O off the audio callback. Treat buffer overflow, input loss, and writer or recording failure as errors that stop capture. Do not silently drop samples and report success.
- Flush encoded audio to disk periodically while recording, on the writer thread, with a five-second default interval. Keep capture memory bounded for recordings lasting hours. A flush failure must stop capture, report a failure, and retain the incomplete recording. Stop still drains captured audio and completes FLAC metadata before promotion.
- Preserve captured audio that can still be finalized when a recording fails. Report the failure explicitly and distinguish it from a successful complete recording. Do not promote an unsuccessfully finalized file or claim an incomplete file saved successfully. Keep incomplete files after interruption or a crash and show their status for inspection.
- Lock the target folder, recording prefix, and audio selections during recording and keep another take disabled until stop/finalization completes. Closing during capture offers Continue recording or Stop and quit. Space toggles recording only when a text field or selector does not have focus.
- Target Windows 10/11 x64, macOS 12+ with a universal Intel/Apple Silicon app, and Linux x64 with Ubuntu 22.04+ as the initial tested baseline. Produce a Windows `.exe`, a macOS `.app`, and a Linux AppImage as one portable download per operating system.
- Bundle the Windows JUCE/FLAC dependencies and use a static C++ runtime. Inspect the resulting executable's dependencies to verify the single-executable goal. Validate the Linux AppImage against its declared baseline rather than assuming a bare Linux executable is portable.
- Use GitHub Actions to build and package all three platforms. Build Linux in an Ubuntu 22.04 container on a supported runner, with Ubuntu 24.04 as the accepted runner plan, to retain the initial Linux compatibility baseline. Produce unsigned preview artifacts and document platform launch requirements.
- Prepare source, documentation, and a working Windows build locally first. Preserve the accepted domain documents. Then push a `codex/` implementation branch and open a draft pull request in the existing repository. Attach the created pull request to the Codex chat. Publish a public release only after the user reviews the working build.

## Testing decisions

- Issue #10 adds focused public boundaries for translation/resolution, injectable OS-language adapter inputs, structured status presentation, and caption-independent native controls. These checks extend the original session boundary for the new localization requirements. Local Windows render/input evidence does not establish macOS/Linux runtime acceptance.

- The user confirmed one main automated test boundary on 2026-10-11: exercise a recording session with known input audio and a real temporary target folder. Observe filenames, resulting FLAC contents, session outcomes, and file finalization. Test externally visible behavior rather than private methods, internal queues, or how modules call one another.
- There is no existing application test suite or established test boundary in the repository. The proposed recording-session boundary supplies the highest practical point for the accepted filename, capture, finalization, and failure checks. Prefer exercising filename behavior through the session over creating a second filename-specific test API.
- Use controlled audio input and controlled input/write failures at the session's integration points. Keep real filesystem behavior and real FLAC encoding/decoding in successful recording checks. Test the recording-session behavior and its filename/FLAC outcomes together.
- Verify initial counter 01, the highest-counter-plus-one rule, exact-prefix matching, gaps, at-least-two-digit padding, and the transition from 99 to 100. Verify that unrelated prefixes do not affect allocation and that both final and incomplete recording names occupy counters.
- Verify preview updates after target-folder or prefix changes, rejection of unsafe prefixes, and recording refusal for missing or unwritable target folders. Confirm that rejected configurations do not create recordings elsewhere or silently create the requested missing folder.
- Exercise two recording sessions against the same target folder and prefix. Change folder contents between preview and start, race concurrent reservations, and introduce a final-name conflict before promotion. Verify that existing files remain unchanged and each session either uses a safe distinct name or reports a failure. No check may rely on replacing an existing file.
- Feed known mono and stereo signals, then decode the saved FLAC with an independent read step. Check sample rate, channel count and ordering, 16-bit metadata, frame count, and expected sample quantization. Use more device input channels than the selected output channels to verify that the recording contains exactly the chosen mono input or stereo pair. Confirm that the app does not resample or add sound processing.
- Verify that stopping drains queued captured audio, finalizes the file, and promotes the partial filename only after success. Check that a second take remains unavailable until finalization completes and that repeated takes retain the prefix while advancing the counter.
- Verify periodic flushing during an active recording through the recording-session output boundary and a real partial file. Check that its encoded bytes become visible before Stop and that a controlled flush failure reports an incomplete failed take. Validate the default interval with a real Windows recording lasting longer than five seconds.
- Simulate input loss, writer failure, and buffer overflow. Verify that capture stops, the session reports the failure, and it preserves any audio that can still be finalized. Verify incomplete-file retention when finalization cannot succeed and distinguish failed or incomplete outcomes from successful saves.
- Include repeatable native UI acceptance checks for the compact layout, idle and active selected-channel meters, clipping indication, elapsed time, displayed rate and bit depth, mono/stereo selectors, recording-time control locks, keyboard focus behavior, and the close-during-recording choices.
- Check preference restoration across launches, a fresh local date/time prefix on each launch, missing remembered input handling, missing remembered folder handling, and actionable microphone permission errors. Use real platform checks for device enumeration, permissions, and behavior that a simulated recording session cannot establish.
- Build and package Windows x64, macOS universal, and Linux x64 through CI. Check the Windows executable's dependencies, the macOS bundle's architecture/deployment target, and the Linux AppImage against the initial supported baseline. Confirm that each preview includes usable launch instructions for its unsigned status.
- Require a real microphone recording on Windows locally, including inspection of the saved FLAC. Record the device/audio driver used and the observed input stream rate. List outstanding macOS/Linux microphone, permission, and device checks in the draft pull request and preview documentation. CI build/package success alone does not establish recording correctness on real hardware.
- Delivery is complete when the local Windows build is runnable, required automated checks and platform build/package checks have evidence, the Windows microphone check has evidence, and the implementation branch/draft pull request and unsigned previews are available. Report any outstanding macOS/Linux hardware validation explicitly before claiming full recording validation. Public release remains subject to the user's build review.

## Out of scope

- Pause/resume within a recording, playback, editing, speaker monitoring, and a waveform display.
- A separate settings window, user-selectable sample-rate or buffer-size controls, software gain, normalization, noise reduction, and other added sound processing.
- Recording all channels from a multichannel input device rather than the selected mono input or stereo pair.
- DirectSound and a separate native PipeWire audio driver.
- Automatic repair or recovery of incomplete recordings after a crash.
- Creating missing target folders, silently substituting another target folder or a remembered missing input device, reusing counter gaps, or overwriting recordings.
- Architectures beyond Windows x64, macOS universal Intel/Apple Silicon, and Linux x64. A broader Linux distribution compatibility promise awaits validation.
- Signing and macOS notarization, which require a later credential/setup step. Public release publication before the user's review of the working build.

## Further notes

This specification synthesizes the 19 decisions accepted across three design interview rounds and the recording-session test boundary confirmed during spec preparation. Use the domain terms Recording, Target folder, Input device, Level meter, Recording prefix, Counter, Mono input, Stereo pair, Audio driver, Input stream rate, and Incomplete recording consistently.

The accepted framework version is a pin, not a claim that future implementations should adopt the latest release. The design research was checked on 2026-10-11. Relevant primary references include the [JUCE 9.0.3 release](https://github.com/juce-framework/JUCE/releases/tag/9.0.3), [JUCE FLAC format documentation](https://docs.juce.com/master/classjuce_1_1FlacAudioFormat.html), [JUCE input-device documentation](https://docs.juce.com/master/classjuce_1_1AudioIODevice.html), [JUCE licensing](https://raw.githubusercontent.com/juce-framework/JUCE/master/LICENSE.md), [JUCE 9.0.3 platform requirements](https://raw.githubusercontent.com/juce-framework/JUCE/9.0.3/README.md), [AppImage build-baseline guidance](https://docs.appimage.org/introduction/concepts.html), and [PipeWire JACK compatibility](https://docs.pipewire.org/page_man_pw-jack_1.html). Recheck tooling details when needed during implementation while retaining the accepted product requirements.

The user requested publication of this implementation specification through `to-spec`, with the `ready-for-agent` triage state. The public-release review requirement remains part of the accepted delivery workflow.
